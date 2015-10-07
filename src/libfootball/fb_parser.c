///
/// Football command parsing.
/// @file       fb_parser.c - Football socket abstraction layer
/// @author     Perette Barella
/// @date       2012-03-06
/// @copyright  Copyright 2012–2014 Devious Fish. All rights reserved.
///

/** @details
 *
 *	This file contains functions for adding/identifying command lines.
 *
 *	fb_create_parser creates an empty parser.  fb_parser_add_statements then accepts
 *	an array of strings/return values (FB_PARSER_DEFINITION) to add statements to
 *	the parser.  Multiple arrays of definitions can be added to a single parser.
 *
 *	Statement formats can be composed of:
 *		- keyword		-- a bare word.
 *		- {value}		-- accepts any value in that position
 *      - {#value}      -- Accepts decimal numeric value in that position
 *      - {#value:3-5}  -- Accepts numeric range in that position.  See range note below.
 *		- <one|two>     -- accepts any 1 of the required words in that position
 *		- [optional]	-- accepts an optional word in the current position
 *      - [four|five]   -- accepts 0 or 1 optional words in the current position
 *		- [{value}]     -- accepts an optional value, only as the final word (... may follow)
 *		- ...			-- allows 0 or more additional parameters
 *
 * Ranges:
 *      - If either min or max in range have a decimal point, values are treated as
 *        doubles.  Otherwise, they are considered integers.
 *      - If either min or max has a leading zero, it enables radix detection:
 *        0 -> octal, 0x -> hex, otherwise decimal.
 *      - If neither max nor min has a leading 0, only base 10 is accepted.
 *
 *	Examples: @code
 *		{ CREATE_USER,	"create <admin|guest|user> {username} {password}" }
 *					-- Recognizes the 3 types of users and requires username & password parameter, nothing more.
 *		{ CREATE_USER_EXTENDED, "create <admin|guest|user> {username} {password} {additional} ..." }
 *					-- Recognizes the 3 types of users and requires username, password, & at least 1 additional parameter.
 *		{ CREATE_USER_OPTIONAL_EXTENDED, "create <admin|guest|user> {user} {password} ... }
 *					-- Recognizes the 3 types of users, requires username and password, allows additional parameters.
 *					   This definition is incompatible with CREATE_USER above--the parser would error when adding this
 *					   definition.  (There is no way to delineate that command and this command with 0 parameters.)
 *  @endcode
 *		A completely different approach to the above would be: @code
 
 *		{ CREATE_ADMIN_USER,	"create admin {username} {password} ..." },
 *		{ CREATE_STANDARD_USER,	"create standard {username} {password} ..." },
 *		{ CREATE_GUEST_USER,	"create guest {username} {password} ..." }
 *					-- Returns different values for the different user types, so you can determine that
 *					   from the command's enumeration instead of having to further parse. As written, these
 *					   would each allow additional optional parameters beyond username and password.
 *  @endcode
 * Since the parse tree is assumed to be hard-coded in your application, this module simply assert()s if it
 * finds problems in the parse definitions.  NDEBUG should not be defined in the development environment to
 * ensure these are caught; since statements are tested for validity in development, removal of the assertions
 * is acceptable for release.
 *
 * @see Football documentation.
 */

#include <config.h>

#ifndef __FreeBSD__
#define _BSD_SOURCE /* snprintf() */
#endif

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <ctype.h>
#include <assert.h>
#include <search.h>
#include <sys/types.h>

#include "fb_public.h"
#include "fb_service.h"

/** Any given token can represent... */
typedef enum fb_parsecomponent_t {
	FB_PARSER_UNDETERMINED,
	FB_PARSER_KEYWORD,
	FB_PARSER_VALUE,
	FB_PARSER_OPENEND
} FB_PARSER_TYPE;

/** Possible value types */
typedef enum fb_valuetype_t {
    FB_VALUE_STRING, /**< Value may be any string */
    FB_VALUE_NUMERIC, /**< Value must be a decimal integer, positive or negative. */
    FB_VALUE_RANGED_INTEGER, /**< Value must be an integer within a range */
    FB_VALUE_RANGED_REAL /**< Value may be any number within a range */
} FB_VALUETYPE;

/** Structure of the compiled parse tree */
struct fb_parser_t {
	FB_PARSER_TYPE type; /**< Type of this element */
	FB_VALUETYPE value_type; /**< If type is FB_PARSER_VALUE, value types allowed */
	char *word; /**< term, or {valuename} */
    char *name; /**< name of term or value */
    bool is_named; /**< Whether to apply name for this keyword */
	int response; /**< Command ID, if this point in parse tree represents a complete statement;
                       0 otherwise. */
	size_t subcomponent_count; /**< Number of branches from this point in parse tree */
	size_t subcomponents_size; /**< Capacity of subcomponents array */
	struct fb_parser_t *subcomponents; /**< Branches from this point in parse tree */
    long min_integer; /**< Minimum value for FB_VALUE_RANGED_INTEGER value types */
    long max_integer; /**< Maximum value for FB_VALUE_RANGED_INTEGER value types */
    double min_double; /**< Minimum value for FB_VALUE_RANGED_REAL value types */
    double max_double; /**< Maximum value for FB_VALUE_RANGED_REAL value types */
    int radix; /**< Input radix for ranged integers: 0=autodetect; 8, 10, 16 allowed */
};



/** @internal
    Create an argv-style array.
    A command line with nothing on it results in 0 and a pointer to an array
    with one null.
    @param commandline The command to split up.  This original string remains
    unaltered, but pointers into it are created for arg_r.
    @param result The place to put the argv array.
    @param remainders Pointers corresponding to keywords in argv, that
    include the remainder of the command line instead of just one term.
    @return The number of populated terms in the argv array.
            On error, returns a negative number and a null pointer.
   */
int fb_create_argv (const char *commandline, char ***result, char ***remainders) {
	/* ***result: Anything can be solved with sufficient indirection */
	/* Skip leading whitespace */
	while (*commandline && isspace (*commandline)) {
		commandline++;
	}
	/* Make a copy of the command line to scratch up */
	char *command = strdup (commandline);
	if (command == NULL) {
        fb_perror ("strdup");
		*result = NULL;
		return -1;
	}
	/* First get a quick count of the words. */
	int wordcount = 0;
	char *c = command;
	while (*c) {
		wordcount++;
		while (*c && !isspace (*c)) {
			c++;
		}
		while (*c && isspace (*c)) {
			c++;
		}
	}
	/* Allocate a vector for the pointers and populate it */
	char **argv = (char **) calloc((wordcount + 1) * 2, sizeof (char *));
	if (argv == NULL) {
        fb_perror ("calloc");
		free (command);
		*result = NULL;
		return -1;
	}
    char **argr = argv + wordcount + 1;
	c = command;
	wordcount = 0;
	while (*c) {
		argv [wordcount] = c;
        argr [wordcount++] = (char *) commandline + (c - command);
		if (*c == '\"') {
            c++;
			/* Quoted string */
			while (*c) {
				if (*c == '\"' && (isspace (*(c+1)) || *(c+1) == '\0')) {
					break;
				}
				c++; /* Find quote AT END OF WORD.  Ignores quotes in words. */
			}
            /* Skip first byte */
            if (wordcount == 1) {
                /*  Can't skip byte on argv [0]:
                 First parameter is the address used by malloc()/free().
                 Shift the text in the string instead. */
                /* Memcpy doesn't deal with overlaps; memmove does. */
                memmove(argv [0], argv [0] + 1, c - argv [0]);
                *(c-1) = '\0';
            } else {
                /* For other elements, just bump the pointer */
                argv [wordcount - 1]++;
            }
		} else {
			/* Plain word */
			while (*c && !isspace (*c)) { /* skip the word */
				c++;
			}
		}
		if (*c) { /* Null terminate if needed */
			*(c++) = '\0';
		}
		while (*c && isspace (*c)) { /* Skip whitespace */
			c++;
		}
	}
	assert (argv [wordcount] == NULL);
    assert (argr [wordcount] == NULL);
	if (wordcount == 0) {
		/* The command isn't actually used, so free it before it leaks */
		free (command);
	}
	*result = argv;
    *remainders = argr;
	return wordcount;
}

		   
/** @internal
    Free up resources used by an argv array.
    @param argv The array to release. */
void fb_destroy_argv (char **argv) {
	if (argv) {
		/* The array is built from chopped up pieces of one line, so just free the first. */
		free (*argv);
		/* And free the vector itself */
		free (argv);
	}
}



/** Create a new, empty parser.
    @return The new parser, or NULL on error.*/
FB_PARSER *fb_create_parser (void) {
	FB_PARSER *parser;
	if ((parser = calloc (1, sizeof *parser))) {
		parser->type = FB_PARSER_UNDETERMINED;
	} else {
        fb_perror ("calloc");
	}
	return (parser);
}


/** @internal
    A qsort() etc compliant callback that compares keywords in parse tree branches.
    @param elem1 the first item for comparison.
    @param elem2 the other item for comparison.
    @return Negative, 0, or positive value in the usual manner. */
static int fb_parse_compare (const void *elem1, const void *elem2) {
	return strcasecmp (((FB_PARSER *) elem1)->word, ((FB_PARSER *) elem2)->word);
}



static bool fb_parser_add (FB_PARSER *parser, const int response, char **argv);

/** @internal
    Add a keyword branch to a parse tree.
    @Note argv[0] is not necessarily the same as keyword.
          For example, with alternation argv[0] == '<a|b|c>' and keyword == 'b'.
    @param parser The parse tree to add the branch to.
    @param response The command ID
    @param argv The remaining terms in this command pattern.
    @param keyword The keyword to add.
    @param named Whether the keyword is part of a named alternation or optional.
 */
static bool fb_parser_add_keyword (FB_PARSER *parser, const int response, char **argv, char *keyword,
                                   const char *name) {
	/* Expand the array to accommodate new elements if required. */
    if (!fb_expandcalloc((void **) &parser->subcomponents, &parser->subcomponents_size,
                         parser->subcomponent_count + 1, sizeof (FB_PARSER))) {
        fb_perror ("fb_expandcalloc");
        return 0;
	}

    /* Set up/check the keyword, if applicable */
    if (name) {
        if (parser->name) {
            if (strcmp (parser->name, name) != 0) {
                assert (0);
                return false;
            }
        } else {
            if (!(parser->name = strdup (name))) {
                fb_perror ("strdup");
                return false;
            }
        }
    }

	/* See if this word already exists. */
	FB_PARSER *newitem = &(parser->subcomponents [parser->subcomponent_count]);
	newitem->word = keyword;
	FB_PARSER *found = lfind (newitem, parser->subcomponents, &parser->subcomponent_count, sizeof (FB_PARSER), fb_parse_compare);
	if (found == NULL) {
		if (!(newitem->word = strdup (keyword))) {
            fb_perror ("strdup");
			return false;
		}
		parser->subcomponent_count += 1;
		newitem->type = FB_PARSER_UNDETERMINED;
        newitem->is_named = (name != NULL);
		found = newitem;
    } else {
        assert (found->is_named == (name != NULL));
        if (found->is_named != (name != NULL)) {
            fb_log (FB_WHERE (FB_LOG_ERROR), "Keyword %s is used in both named and unnamed instances", keyword);
            return false;
        }
    }
	/* Recurse on the next term */
	return fb_parser_add (found, response, argv+1);
}


/** @internal
    Add a fill-in-the-blank to the parse tree
    @param parser The parse tree to add the keyword to.
    @param response The command ID
    @param argv The remaining terms in this command pattern.
    @param blankname The name of the blank being added.
 */
static bool fb_parser_add_fill_in (FB_PARSER *parser, const int response, char **argv, char *blankname) {
    long min_integer = 0, max_integer = 0;
    double min_double = 0, max_double = 0;
    int radix = 10;
    /* If this is a numeric input, determines its range and base */
    bool numeric = (blankname [1] == '#');
    FB_VALUETYPE value_type = FB_VALUE_STRING;
    if (numeric) {
        value_type = FB_VALUE_NUMERIC;
        char *range = strchr (blankname, ':');
        if (range) {
            range++;
            value_type = (strchr (blankname, '.') ? FB_VALUE_RANGED_REAL : FB_VALUE_RANGED_INTEGER);
            char *upperrange = strchr (*range == '-' ? range + 1 : range, '-'); /* Might have negative min value */
            assert (upperrange);
            char *error;
            if (value_type == FB_VALUE_RANGED_REAL) {
                min_double = strtod (range, &error);
                assert (error && error == upperrange);
                max_double = strtod (upperrange + 1, &error);
                assert (error && *error == '}');
                assert (min_double < max_double);
            } else {
                if ((*range == '0' && *(range + 1) != '-') ||
                    (*range == '-' && *(range + 1) == '0') ||
                    (*upperrange == '0' && *(range + 1) != '}') ||
                    (*upperrange == '-' && *(upperrange + 1) == '0')) {
                    radix = 0;
                }
                min_integer = strtol(range, &error, radix);
                assert (error && error == upperrange);
                max_integer = strtol(upperrange + 1, &error, radix);
                assert (error && *error == '}');
                assert (min_integer < max_integer);
            }
        }
    }
    /* Locate and size the name */
    char *startname = blankname + 1;
    if (*startname == '#') {
        startname++;
    }
    char *endname = strchr (startname, ':');
    if (!endname) {
        endname = strchr (startname, '}');
    }
    assert (endname);
    size_t namelength = endname - startname;
	if (parser->type == FB_PARSER_VALUE) {
        /* Make sure the existing parser item is the same as the new one */
		assert (parser->subcomponents_size == 1);
		assert (parser->subcomponent_count == 1);
        assert (parser->value_type == value_type);
		assert (parser->value_type != FB_VALUE_RANGED_REAL || (parser->min_double == min_double && parser->max_double == max_double));
		assert (parser->value_type != FB_VALUE_RANGED_INTEGER || (parser->min_integer == min_integer && parser->max_integer == max_integer));
        /* Check word names match */
        if (namelength == 0) {
            assert (!parser->name);
        } else {
            assert (strlen (parser->name) == namelength);
            assert (strncmp (parser->name, startname, namelength) == 0);
        }
	} else if (parser->type == FB_PARSER_UNDETERMINED) {
        /* Create a new parse tree branch for the value */
        parser->type = FB_PARSER_VALUE;
        char *word = strdup (blankname);
        char *name = namelength == 0 ? NULL : strdup (startname);
        if (word && (namelength == 0 || name)) {
            if ((parser->subcomponents = fb_create_parser())) {
                parser->subcomponents_size = 1;
                parser->subcomponent_count = 1;
                parser->subcomponents->word = word;
                endname = strchr (parser->subcomponents->word, ':');
                if (endname) {
                    *(endname++) = '}';
                    *(endname++) = '\0';
                }
                parser->name = name;
                if (name) {
                    parser->name [namelength] = '\0';
                }
                parser->value_type = value_type;
                parser->min_integer = min_integer;
                parser->max_integer = max_integer;
                parser->min_double = min_double;
                parser->max_double = max_double;
                parser->radix = radix;
            } else {
                free (word);
                free (name);
                return false;
            }
		} else {
            fb_perror ("strdup");
			return false;
		}
	} else {
		assert (0);
        fb_log (FB_WHERE (FB_LOG_ERROR), "Cannot use {} alongside other term types.");
		return false;
	}
	/* Recurse on the next term */
	return fb_parser_add (parser->subcomponents, response, argv+1);
}



/** @internal
    Add a new command to the parse tree.
    @param parser The parse tree to add the keyword to.
    @param response The command ID
    @param argv The remaining terms in this command pattern.  Argv[0] is the item to add. */
static bool fb_parser_add (FB_PARSER *parser, const int response, char **argv) {
	assert (parser);
	assert (parser->subcomponents_size >= parser->subcomponent_count);
	
	char *word = *argv;
	size_t wordlen = word ? strlen (word) : 0;
	
	/* Handle end of command or optional fill-in-the-blank at end of command */
	if (!word ||
		(word [0] == '[' && word [1] == '{' && word [wordlen - 2] == '}' && word [wordlen - 1] == ']')) {
		if (word && *(argv+1)) {
            /* Allow optionals to be followed by ... only */
            if (*(argv+2) != NULL || strcasecmp (*(argv+1), "...") != 0) {
                assert (0);
                fb_log (FB_WHERE (FB_LOG_ERROR), "Optional fill-in-the-blank must be the last word.");
                return (false);
            }
		}
		if (parser->response) {
			assert (0);
			fb_log (FB_WHERE (FB_LOG_ERROR), "Statement redefined.");
			return false;
		}
		parser->response = response;
		bool result = true;
		if (word) {
			// Take the optional-brackets off when we pass to fill-in subroutine.
			word [wordlen - 1] = '\0';
			result = fb_parser_add_fill_in (parser, response, argv, word + 1);
			// Put back in case we're invoked again via <alternation> or [optional].
			word [wordlen - 1] = ']';
		}
		return result;
	}

	/* Deal with open-ended commands (elipsis) */
	if (strcmp (word, "...") == 0) {
		if (parser->type != FB_PARSER_UNDETERMINED || parser->response) {
			assert (0);
			fb_log (FB_WHERE (FB_LOG_ERROR), "Can not use ... alongside other term types.");
			return false;
		}
		parser->type = FB_PARSER_OPENEND;
		parser->response = response;
		return true;
	}

	/* Handle fill-in-the-blanks */
	if (word [0] == '{' && word[wordlen - 1] == '}') {
		/* Recurse on the next term */
		return fb_parser_add_fill_in (parser, response, argv, word);
	}

	/* If none of the others fit, it must be a keyword match */
	if (parser->type == FB_PARSER_UNDETERMINED) {
		parser->type = FB_PARSER_KEYWORD;
	} else if (parser->type != FB_PARSER_KEYWORD) {
		assert (0);
		fb_log (FB_WHERE (FB_LOG_ERROR), "Can not use keyword alongside other types except full stop.");
		return false;
	}
	
	/* Keyword list/alternation, including optional ones */
	if ((word [0] == '<' && word[wordlen - 1] == '>') ||
        (word [0] == '[' && word[wordlen - 1] == ']')) {
		bool ok = true;
        bool autoname = false;
        /* Deal with the [optional] case */
        if (word [0] == '[') {
            autoname = true;
            ok = fb_parser_add (parser, response, argv + 1);
            if (!ok) {
                fb_log (FB_WHERE (FB_LOG_ERROR),
                        "Previous errors refers to when optional word(s) %s omitted", word);
            }
        }
        /* Now do it while adding the keyword list. */
		char *commandcopy = strdup ((word)+1);
		if (!commandcopy) {
            fb_perror ("strdup");
			return false;
		}
        commandcopy [strlen (commandcopy) - 1] = '\0';
        char *this_word = commandcopy;
        char *name = commandcopy;
        /* If there's a name there, extract it and skip it */
        char *endname = strchr(commandcopy, ':');
        if (endname) {
            autoname = false;
            *endname = '\0';
            this_word = endname + 1;
        }
		/* Repeat for each word in the alternation */
		while (*this_word) {
			char *endword = this_word;
			while (*endword && *endword != '|') {
				endword++;
			}
			char *nextword = *endword ? endword + 1 : endword;
            if (*endword == '|') {
                autoname = false;
            }
			*endword = '\0';
			if (this_word != endword) { /* Don't allow zero-length words */
                if (autoname) {
                    name = this_word;
                }
				ok = fb_parser_add_keyword (parser, response, argv, this_word, name) && ok;
			}
			this_word = nextword;
		}
		free (commandcopy);
		return (ok);
	}
	return fb_parser_add_keyword (parser, response, argv, word, NULL);
}



/** @internal
    Recursively descend the parse tree, sorting all the keyword values
    so we can bsearch(3) instead of lsearch(3) on them later.
    @param parser the parser to sort
*/
static void fb_sort_all (FB_PARSER *parser) {
	/* If only 1 subcomponent, skip sort. */
	if (parser->subcomponent_count > 1) {
		qsort (parser->subcomponents, parser->subcomponent_count, sizeof (FB_PARSER), fb_parse_compare);
	}
	unsigned int i;
	for (i = 0; i < parser->subcomponent_count; i++) {
		fb_sort_all (&(parser->subcomponents[i]));
	}
}

						  
/** Add statements to the parser.
    @param parser The parser to add statements to.
    @param def The command pattern definitions.
    @param count The number of pattern definitions.
    @return true on success, false on failure. */
bool fb_parser_add_statements (FB_PARSER *parser, const FB_PARSE_DEFINITION def[], const size_t count) {
	char **argv;
	int response;
	size_t i;
	/* For each command line in the array... */
	for (i = 0; i < count; i++) {
        char **unused;
		if ((response = fb_create_argv (def[i].statement, &argv, &unused)) >= 0) {
			response = fb_parser_add (parser, def[i].response, argv);
			fb_destroy_argv (argv);
			if (!response) {
                fb_log (FB_WHERE (FB_LOG_ERROR), "Defective statement is: %s", def [i].statement);
                return false;
			}
		} else if (response < 0) {
			return false;
		}
	}
	fb_sort_all (parser);
	return true;
}




/** @internal
    Interpret one token of a command in an argv array.  If the command is complete,
    return that number; if it continues, recurse; if it is invalid, return the
    appropriate problem type code.
    @param parser The point we are at in the parse tree.
    @param argv An argv-style array of remaining terms.
    @param argname If not-null, array is populated with names of named argv elements.
    @param errorterm On error, set to the missing term, invalid term, or bad value name.
    On success, set to point to the first unconsumed term.
    @return The command number for a valid command, or a parser error code representing
            the form of invalidity. */

int fb_interpret_recurse (const FB_PARSER *parser, char *const *argv, char **argname, char **errorterm) {
    assert (parser);
    assert (argv);
    assert (errorterm);
    FB_PARSER matchthis;
    FB_PARSER *found;

    *errorterm = *argv;
    if (*argv == NULL && parser->response) {
        return parser->response;
    }
    if (*argv == NULL) {
        *errorterm = parser->word;
        return FB_PARSE_INCOMPLETE;
    }
    if (parser->name && *argv) {
        fb_log (FB_WHERE (FB_LOG_PARSER), "%s %s-->%s",
                parser->type == FB_PARSER_VALUE ? "Value" : "Keyword",
                parser->name, *argv);
    }
    switch (parser->type) {
        case FB_PARSER_UNDETERMINED:
            return FB_PARSE_EXTRA_TERMS;
        case FB_PARSER_OPENEND:
            return parser->response;
        case FB_PARSER_KEYWORD:
            matchthis.word = *argv;
            found = bsearch (&matchthis, parser->subcomponents, parser->subcomponent_count, sizeof (FB_PARSER), fb_parse_compare);
            if (found == NULL) {
                return FB_PARSE_INVALID_KEYWORD;
            }
            if (found->is_named && argname) {
                *argname = parser->name;
            }
            return fb_interpret_recurse (found, argv+1, argname ? argname + 1 : NULL, errorterm);
        case FB_PARSER_VALUE:
            if (argname) {
                *argname = parser->name;
            }
            switch (parser->value_type) {
                case FB_VALUE_RANGED_INTEGER:
                {
                    char *error;
                    long value = strtol(*argv, &error, parser->radix);
                    if (error && *error) {
                        return FB_PARSE_NUMERIC;
                    }
                    if (value < parser->min_integer ||
                        value > parser->max_integer) {
                        return FB_PARSE_RANGE;
                    }
                }
                    break;
                case FB_VALUE_RANGED_REAL:
                {
                    char *error;
                    double value = strtod(*argv, &error);
                    if (error && *error) {
                        return FB_PARSE_NUMERIC;
                    }
                    if (value < parser->min_double ||
                        value > parser->max_double) {
                        return FB_PARSE_RANGE;
                    }
                }
                    break;
                case FB_VALUE_NUMERIC:
                {
                    /* Verify the value is a numeric. */
                    char *c = *argv;
                    if (*c == '-') {
                        c++;
                    }
                    while (*c) {
                        if (!isdigit (*c)) {
                            return FB_PARSE_NUMERIC;
                        }
                        c++;
                    }
                }
                    break;
                case FB_VALUE_STRING:
                    break;
            }
            if (*argv) {
                return fb_interpret_recurse (parser->subcomponents, argv+1,
                                             argname ? argname + 1 : NULL, errorterm);
            }
            *errorterm = parser->word;
            return FB_PARSE_INCOMPLETE;
    }
    assert (0);
    return FB_PARSE_FAILURE;
}

/** Given a parser and an argv array with a command, lookup the command number.
    @param parser The point we are at in the parse tree.
    @param argv An argv-style array of remaining terms.
    @param errorterm On error, set to the missing term, invalid term, or bad value name.
    On success, points to the first term unmatched.  If all terms were used, or none for
    the elipsis, points to a null.
    @return the command number, or one of the FB_PARSER_* values to indicate
    the nature of invalidity. */
int fb_interpret (const FB_PARSER *parser, char *const *argv, char **errorterm) {
    assert (parser);
    assert (argv);
    assert (errorterm);
    return fb_interpret_recurse (parser, argv, NULL, errorterm);
}


/** @internal
    Destroy a parser and free up its resources.
    @param parser the parser to destroy. */
static void fb_parser_destroy_recurse (FB_PARSER *parser) {
    assert (parser);
	unsigned int i;
	for (i = 0; i < parser->subcomponent_count; i++) {
		fb_parser_destroy_recurse (&(parser->subcomponents [i]));
	}
	free (parser->word);
	free (parser->subcomponents);
}


/** Destroy a parser and free up its resources.
    @param parser The parser to destroy. */
void fb_parser_destroy (FB_PARSER *parser) {
    assert (parser);
	fb_parser_destroy_recurse (parser);
	free (parser);
}
