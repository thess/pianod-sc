/*
 *  users.c -- user management provider
 *  pianod
 *
 *  Created by Perette Barella on 2012-03-20.
 *  Copyright 2012–2014 Devious Fish. All rights reserved.
 *
 */

#ifndef __FreeBSD__
#define _BSD_SOURCE /* snprintf() */
#endif

#include <config.h>

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <unistd.h>

/* crypt() is in unistd.h for BSD. */
#ifdef HAVE_CRYPT_H
#include <crypt.h>
#endif

#include <ezxml.h>
#include <fb_public.h>

#include "users.h"
#include "response.h"
#include "logging.h"
#include "pianod.h"
#include "tuner.h"

typedef enum find_kind_t {
	FIND_OPEN_CONNECTIONS,
	FIND_ALL_CONNECTIONS
} FIND_KIND;

/* Data about each user */
struct user_t {
	char *name;
	char *password;
	USER_RANK rank;
	bool privileges[PRIVILEGE_COUNT];
    CREDENTIALS pandora_credentials;
	struct station_preferences_t *station_preferences;
	struct user_t *next;
};

typedef struct rankings_t {
	const char *name;
	USER_RANK value;
} RANKINGS;

static RANKINGS rankings[] = {
	{ "disabled",	RANK_NONE },
	{ "guest",		RANK_LISTENER },
        /* Deprecated, but we must continue using guest to avoid
           breaking older clients.  Clients should be updated to
           accept either; this will change and break things
           on or after July 1, 2014. */
	{ "listener",	RANK_LISTENER },
	{ "user",		RANK_STANDARD },
	{ "admin",		RANK_ADMINISTRATOR }
};

typedef struct privileges_t {
	const char *name;
	PRIVILEGE index;
	bool initial_value;
	bool persistable;
} PRIVILEGES;

static PRIVILEGES privileges[] = {
	{ "owner",		PRIVILEGE_MANAGER,		false,	false },
	{ "service",	PRIVILEGE_SERVICE,		false,	true },
	{ "influence",	PRIVILEGE_INFLUENCE,	true,	true },
	{ "tuner",		PRIVILEGE_TUNER,		false,	true },
	{ "present",	ATTRIBUTE_PRESENT,		false,	true }
};

typedef struct managers_t {
    const char *name;
    MANAGER_RULE index;
} MANAGER_RULES;

static MANAGER_RULES manager_rules[] = {
    { "mine",           MANAGER_USER },
    { "none",           MANAGER_NONE },
    { "administrator",  MANAGER_ADMINISTRATOR }
};

static USER *user_list;
static bool dirty = false; /* Whether the user data has unsaved changes */
static USER_RANK visitor_rank = RANK_LISTENER; /* Fucking aliens and their rainbows */
static MANAGER_RULE ownership_rule = MANAGER_ADMINISTRATOR;

/* Cipher function from lamercipher.c */
char *lamer_cipher (const char *keystr, const char *item);

#define countof(x) (sizeof (x) / sizeof (*x))

USER_RANK get_rank_by_name (const char *name) {
	assert (name);
	int i;
	for (i = 0; i < countof (rankings); i++) {
		if (strcasecmp (name, rankings [i].name) == 0) {
			return rankings [i].value;
		}
	}
	assert (0);
	return RANK_NONE;
}

const char *user_type_name (USER_RANK rank) {
	int i;
	for (i = 0; i < countof (rankings); i++) {
		if (rank == rankings [i].value) {
			return rankings [i].name;
		}
	}
	assert (0);
	return NULL;
}

static const PRIVILEGES *get_privilege_by_name (const char *name) {
	assert (name);
	int i;
	for (i = 0; i < countof (privileges); i++) {
		if (strcasecmp (name, privileges [i].name) == 0) {
			return &privileges [i];
		}
	}
	return NULL;
}

PRIVILEGE get_privilege_id_by_name (const char *name) {
	const PRIVILEGES *priv = get_privilege_by_name (name);
	assert (priv);
	return priv->index;
}

static const PRIVILEGES *get_privilege_by_id (PRIVILEGE priv) {
	int i;
	for (i = 0; i < countof (privileges); i++) {
		if (priv == privileges [i].index) {
			return &privileges [i];
		}
	}
	assert (0);
	return NULL;
}


static const MANAGER_RULES *get_manager_rule_by_name (const char *name) {
	assert (name);
	int i;
	for (i = 0; i < countof (manager_rules); i++) {
		if (strcasecmp (name, manager_rules [i].name) == 0) {
			return &manager_rules [i];
		}
	}
	return NULL;
}

static MANAGER_RULE get_manager_rule_id_by_name (const char *name) {
	const MANAGER_RULES *mr = get_manager_rule_by_name (name);
	assert (mr);
	return mr->index;
}

static const MANAGER_RULES *get_manager_rule_by_id (MANAGER_RULE mr) {
	int i;
	for (i = 0; i < countof (manager_rules); i++) {
		if (mr == manager_rules [i].index) {
			return &manager_rules [i];
		}
	}
	assert (0);
	return NULL;
}


static FB_CONNECTION *find_online_user (FB_SERVICE *service, USER *user, FIND_KIND find) {
	assert (service);
	assert (user);

	FB_CONNECTION *found = NULL;
	FB_ITERATOR *it = fb_new_iterator (service);
	if (it) {
		FB_EVENT *event;
		while ((event = fb_iterate_next (it))) {
			if (find == FIND_OPEN_CONNECTIONS && event->type == FB_EVENT_ITERATOR_CLOSE) {
				continue;
			}
			USER_CONTEXT *context = event->context;
			if (context->user == user) {
				found = event->connection;
				break;
			}
		}
		fb_destroy_iterator (it);
	}
	return found;	
}


bool is_user_online (FB_SERVICE *service, USER *user) {
	return find_online_user (service, user, FIND_ALL_CONNECTIONS) != NULL;
}

/* Iterators on users */
USER *get_first_user (void) {
	return user_list;
}

USER *get_next_user (USER *user) {
	assert (user);
	return user->next;
}


/* Destroy Pandora credentials */
void destroy_pandora_credentials (CREDENTIALS *creds) {
    free (creds->username);
    free (creds->password);
    memset (creds, 0, sizeof (*creds));
}


/* Free all users and their data in the given list */
static void destroy_users (USER *list) {
	USER *next;
	while (list) {
		next = list->next;
		free (list->name);
		free (list->password);
        destroy_pandora_credentials (&list->pandora_credentials);
		destroy_station_preferences (list->station_preferences);
		free (list);
		list = next;
	}
}
							 
							 
/* Get a user's record by name */		 
static USER *find_user (const char *username) {
	assert (username);
	USER *user;
	for (user = user_list; user; user = user->next) {
		if (strcasecmp (username, user->name) == 0) {
			return user;
		}
	}
	return NULL;
}

struct user_t *get_user_by_name (FB_EVENT *event, const char *username)
{
	USER *user = find_user (username);
	if (!user) {
		reply (event, E_NOTFOUND);
	}
	return (user);
}

void set_ownership_rule (MANAGER_RULE rule, USER *manager) {
	// Clear prior owner flags
	clear_privilege (PRIVILEGE_MANAGER);
	if (rule == MANAGER_USER) {
		assert (manager);
        manager->privileges [PRIVILEGE_MANAGER] = true;
	}
	ownership_rule = rule;
}



/* Encrypt a password and store it in a user's record */
static char *encrypt_password (const char *password) {
	static char *saltchars = "./0123456789QWERTYUIOPASDFGHJKLZXCVBNMqwertyuiopasdfghjklzxcvbnm";
	assert (strlen (saltchars) == 64);
	char salt[2];
	salt [0] = saltchars [random() % 64];
	salt [1] = saltchars [random() % 64];
	return (crypt (password, salt));
}


/* Create a new user with the username and password.
   Encrypt the password if requested (if not, it's already encrypted).
 Returns true on success, false on duplicate user or other failure */
static USER *create_user (const char *username, const char *password, bool encrypt) {
	assert (username);
	assert (password);
	if (!find_user (username)) {
		USER *newuser = calloc (1, sizeof (USER));
		if (newuser) {
			if ((newuser->name = strdup (username)) &&
				(newuser->password = strdup (encrypt ? encrypt_password (password) : password))) {
				/* Initialize the new user's privileges */
				for (int i = 0; i < countof (privileges); i++) {
					newuser->privileges [privileges [i].index] = privileges [i].initial_value;
				}
				/* Add to the user list */
				newuser->next = user_list;
				user_list = newuser;
				dirty = true;
				return newuser;
			} else {
				perror ("strdup");
			}
			free (newuser->name);
			free (newuser);
		} else {
			perror ("calloc");
		}
	}
	return NULL;
}

/* Change the password in a user's record */
bool set_user_password (USER *user, const char *password) {
	char *newword = strdup (encrypt_password (password));
	if (newword) {
		free (user->password);
		user->password = newword;
		dirty = true;
		return true;
	}
	return false;
}

/* Create a new user with the username and password.
   Returns true on success, false on duplicate user or other failure */
USER *create_new_user (char *username, char *password) {
	assert (username);
	assert (password);
	return create_user (username, password, true);
}


/* Delete a user */
void delete_user (USER *deluser) {
	assert (deluser);
	USER *user, *prior;
	for (prior = NULL, user = user_list; user; prior = user, user = user->next) {
		if (user == deluser) {
			if (prior) {
				prior->next = user->next;
			} else {
				user_list = user->next;
			}
			user->next = NULL;
			destroy_users (user);
			dirty = true;
			return;
		}
	}
	assert (0);
}


/* Logoff users.  If user parameter is null, logs off visitors */
void user_logoff (FB_SERVICE *service, USER *user, const char *message) {
	assert (service);
	
	FB_ITERATOR *it = fb_new_iterator (service);
	if (it) {
		FB_EVENT *event;
		while ((event = fb_iterate_next (it))) {
			USER_CONTEXT *context = event->context;
			if (event->type == FB_EVENT_ITERATOR && context->user == user) {
				send_status (event, message ? message : "Logged off by an administrator");
				fb_close_connection (event->connection);
			}
		}
		fb_destroy_iterator (it);
	}
}



/* Return a user's record, if the user and password are valid */
USER *authenticate_user (const char *username, const char *password) {
	assert (username);
	assert (password);
	USER *user = find_user (username);
	if (user) {
		/* Accept null passwords; must be done by manually editing the password file. */
		assert (user->password);
		if (!*user->password) {
			return (!*password ? user : NULL);
		}
		/* Validate the user's password */
		assert (strlen (user->password) > 2);
		char *encrypted = crypt (password, user->password);
		if (strcmp (encrypted, user->password) == 0) {;
			return (user);
		}
	}
	return NULL;
}


/* Change a user's password, authenticating the old one */
bool change_password (USER *user, const char *old, const char *password) {
	assert (user);
	assert (old);
	assert (password);
	
	USER *auth = authenticate_user (user->name, old);
	if (auth) {
		assert (auth == user);
		return set_user_password (auth, password);
	}
	return false;
}

void set_visitor_rank (USER_RANK rank) {
	visitor_rank = rank;
}

/* Get the rank for a user, or the visitor user rank */
USER_RANK get_effective_rank (USER *user) {
	return user ? user->rank : visitor_rank;
}

/* Determine if a user or visitor has a rank or better */
bool have_rank (USER *user, USER_RANK minimum) {
	return (get_effective_rank (user) >= minimum);
};

/* Set a user's rank */
void set_rank (USER *user, USER_RANK rank) {
	assert (user);
	dirty = dirty || (user->rank != rank);
	user->rank = rank;
}

/* Determine if a user has a privilege; visitors have no privileges */
bool have_privilege (USER *user, PRIVILEGE priv) {
	assert (priv >= 0 && priv < PRIVILEGE_COUNT);
	if (priv == PRIVILEGE_MANAGER) {
		switch (ownership_rule) {
			case MANAGER_ADMINISTRATOR:
				return have_rank (user, RANK_ADMINISTRATOR);
			case MANAGER_NONE:
				return false;
            default:
                /* Other cases continue on unhandled. */
                break;
		}
		assert (ownership_rule == MANAGER_USER);
		/* OWNER_USER, continue on to use user privilege flag */
	}
	/* Administrators inherently get certain privileges */
	if (user && user->rank == RANK_ADMINISTRATOR &&
		(priv == PRIVILEGE_SERVICE || priv == PRIVILEGE_TUNER)) {
		return true;
	}
	return user ? user->privileges [priv] : false;
};

/* Set a user privilege */
void set_privilege (USER *user, PRIVILEGE priv, bool setting) {
	assert (user);
	assert (priv >= 0 && priv < PRIVILEGE_COUNT);
	dirty = dirty || (user->privileges [priv] != setting && get_privilege_by_id (priv)->persistable);
	user->privileges [priv] = setting;
}

bool valid_user_list (FB_EVENT *event, char * const*username) {
	assert (username);
	bool found = true;
	while (*username) {
		if (!find_user (*username)) {
			found = false;
			data_reply (event, I_NOTFOUND, *username);
		}
		username++;
	}
	if (!found) {
		reply (event, E_NOTFOUND);
	}
	return found;
}

/* Disable a privilege for all users */
void clear_privilege (PRIVILEGE priv) {
	for (USER *user = user_list; user; user = user->next) {
		user->privileges [priv] = false;
	}
}

/* Set the requested privilege for the users in the list */
void set_privileges (char * const *username, PRIVILEGE priv, bool setting) {
	assert (username);
	USER *user;
	assert (priv >= 0 && priv < PRIVILEGE_COUNT);
	while (*username) {
		user = find_user (*username);
		set_privilege (user, priv, setting);
		username++;
	}
}

/* Get the user name for a user */
const char *get_user_name (USER *user) {
	assert (user);
	return user->name;
}

struct station_preferences_t *get_station_preferences (USER *user) {
	return user->station_preferences;
}

void set_station_preferences (USER *user, struct station_preferences_t *prefs) {
	user->station_preferences = prefs;
	dirty = true;
}

/* Transmit a user's privileges, or visitor privileges if user parameter is NULL */
void send_privileges (FB_EVENT *event, USER *user) {
	fb_fprintf (event->connection, "%03d %s: %s", I_USER_PRIVILEGES,
				Response (I_USER_PRIVILEGES), user_type_name (user ? user->rank : visitor_rank));
	for (PRIVILEGE p = 0; p < PRIVILEGE_COUNT; p++) {
		const PRIVILEGES *priv = get_privilege_by_id (p);
		assert (priv);
		if (have_privilege (user, p)) {
			fb_fprintf (event->connection, " %s", priv->name);
		}
	}
	fb_fprintf (event->connection, "\n");
}

/* Stash Pandora credentials in a user's record, if indicated to do so by credentials record. */
void save_pandora_credentials (CREDENTIALS *creds) {
    assert (creds->password);
    assert (creds->username);
    if (creds->creator) {
        CREDENTIALS newcred = *creds;
        newcred.manager_rule = creds->manager_rule;
        newcred.username = strdup (creds->username);
        newcred.password = lamer_cipher(creds->username, creds->password);
        if (newcred.username && newcred.password) {
            destroy_pandora_credentials (&creds->creator->pandora_credentials);
            creds->creator->pandora_credentials = newcred;
            dirty = true;
        } else {
            flog (LOG_ERROR, "save_pandora_credentials:strdup: %s", strerror (errno));
        }
    }
}

bool restore_pandora_credentials (USER *user, CREDENTIALS *creds) {
    assert (user);
    assert (creds);
    if (!user->pandora_credentials.username) {
        return false;
    }
    char *pandora_name = strdup (user->pandora_credentials.username);
    char *password = lamer_cipher(user->pandora_credentials.username, user->pandora_credentials.password);
    if (pandora_name && password) {
        destroy_pandora_credentials (creds);
        creds->username = pandora_name;
        creds->password = password;
        creds->manager_rule = user->pandora_credentials.manager_rule;
        creds->manager = user->pandora_credentials.manager;
        return true;
    } else {
        flog (LOG_ERROR, "restore_pandora_credentials:strdup: %s", strerror (errno));
        free (password);
        free (pandora_name);
    }
    return false;
}

/* Called after a user's privilege has been changed to update connected clients.
   If user parameter is NULL, sends to all clients. */
void announce_privileges (FB_SERVICE *service, USER *to_user) {
	assert (service);
	FB_ITERATOR *it = fb_new_iterator (service);
	if (it) {
		FB_EVENT *event;
		while ((event = fb_iterate_next (it))) {
			USER_CONTEXT *context = event->context;
			if (to_user == NULL || context->user == to_user) {
				send_privileges (event, context->user);
			}
		}
		fb_destroy_iterator (it);
	}
}

/* Transmit a user record, either just name or with details */
static void send_user (FB_EVENT *there, USER *user, bool details) {
	reply (there, S_DATA);
	data_reply (there, I_ID, user->name);
	if (details) {
		send_privileges (there, user);
	}
}

/* Send user list */
void send_user_list (FB_EVENT *event, const char *who) {
	bool found = false;
	for (USER *user = user_list; user; user = user->next) {
		if (who == NULL || strcasecmp (who, user->name) == 0) {
			send_user (event, user, true);
			found = true;
		}
	}
	reply(event, who == NULL || found == true ? S_DATA_END : E_NOTFOUND);
}

/* Fake administrator for startscript to use */
USER *get_startscript_user (void) {
	static USER user;
	user.name = "startscript";
	user.rank = RANK_ADMINISTRATOR;
	for (PRIVILEGE p = 0; p < PRIVILEGE_COUNT; p++) {
		user.privileges [p] = true;
	}
	return &user;
}

/* Send list of users presently connected/authenticated */
/* which parameter can be a value from: AUTOTUNE_MODE, PRIVILEGE, or a SEND_USER value.
   For this reason, values for these enums must not collide. */
void send_select_users (FB_SERVICE *service, FB_EVENT *event, int which, bool details) {
	for (USER *user = user_list; user; user = user->next) {
        bool send = false;
        if (which < PRIVILEGE_COUNT) {
            send = have_privilege(user, which);
        } else {
            switch (which) {
                case SEND_USERS_ONLINE:
                    send = find_online_user (service, user, FIND_OPEN_CONNECTIONS);
                    break;
                case SEND_USERS_REMEMBERING_CREDENTIALS:
                    send = user->pandora_credentials.username;
                    break;
                case TUNE_ON_LOGINS:
                    send = have_privilege (user, PRIVILEGE_INFLUENCE) &&
                           find_online_user (service, user, FIND_OPEN_CONNECTIONS);
                    break;
                case TUNE_ON_ATTRIBUTE:
                    send = have_privilege (user, PRIVILEGE_INFLUENCE) &&
                           have_privilege(user, ATTRIBUTE_PRESENT);
                    break;
                case TUNE_ON_ALL:
                    send = have_privilege (user, PRIVILEGE_INFLUENCE) &&
                           (have_privilege(user, ATTRIBUTE_PRESENT) ||
                            find_online_user (service, user, FIND_OPEN_CONNECTIONS));
                    break;
                default:
                    assert (0);
                    break;
            }
        }
		if (send) {
			send_user (event, user, details);
		}
	}
	reply(event, S_DATA_END);
}


/* Resubstantiate a user from the XML userdata file */
static bool recreate_user (ezxml_t userdata) {
	const char *name = ezxml_attr (userdata, "name");
	const char *password = ezxml_attr (userdata, "password");
	const char *rank = ezxml_attr (userdata, "level");
	if (name && password && rank) {
		USER *user = create_user (name, password, false);
		if (user) {
			user->rank = get_rank_by_name (rank);
            /* Read Pandora account data */
            ezxml_t pandora = ezxml_child (userdata, "pandora");
            if (pandora) {
                pandora = ezxml_child (pandora, "user");
                if (pandora) {
                    const char *pname = ezxml_attr (pandora, "name");
                    password = ezxml_attr (pandora, "password");
                    const char *mode = ezxml_attr (pandora, "ownership");
                    char *dupname = strdup (name);
                    if (pname && password && mode && dupname) {
                        const MANAGER_RULES *mr = get_manager_rule_by_name (mode);
                        if (mr) {
                            user->pandora_credentials.manager_rule = mr->index;
                            user->pandora_credentials.manager = user;
                            user->pandora_credentials.username = strdup (pname);
                            user->pandora_credentials.password = strdup (password);
                        } else {
                            flog (LOG_ERROR, "Unknown manager rule %s for user %s\n", mode, user->name);
                        }
                    } else {
                        flog (LOG_ERROR, "Ignored bad Pandora credentials for user %s\n", user->name);
                    }
                }
            }
			/* Read the privileges */
			for (ezxml_t privs = ezxml_child (userdata, "privilege"); privs; privs = privs->next) {
				const char *privname = ezxml_attr (privs, "name");
				const char *granted = ezxml_attr (privs, "granted");
				if (privname && granted) {
					const PRIVILEGES *priv = get_privilege_by_name (privname);
					if (priv) {
						if (priv->persistable) {
							user->privileges [priv->index] = (strcmp (granted, "true") == 0);
						} else {
							flog (LOG_ERROR, "Ignored non-persistable privilege %s for user %s\n",
								  privname, user->name);
						}
					} else {
						flog (LOG_ERROR, "Unknown privilege %s for user %s\n", privname, user->name);
					}
				} else {
					flog (LOG_ERROR, "Privilege data corrupt for user %s\n", user->name);
				}
			}
			ezxml_t preferences = ezxml_child (userdata, "stationpreferences");
			if (preferences) {
				recreate_station_preferences (user, preferences);
			}
			return true;
		}
		flog (LOG_ERROR, "User listed twice in password file: %s\n", name);
	 } else {
		 flog (LOG_ERROR, "User data file corrupt: missing fields for user %s\n",
			  name ? name : "(name unknown)");
	 }
	return false;
}


/* Restore user data from a file */
void users_restore (const char *filename) {
	assert (filename);
	ezxml_t data = ezxml_parse_file (filename);
	if (data) {
		/* Read the data */
		ezxml_t user;
		int user_count = 0;
		int restored_count = 0;
		for (user = ezxml_child (data, "user"); user; user = user->next) {
			user_count++;
			if (recreate_user (user)) {
				restored_count++;
			}
		}
		flog (restored_count < user_count ? LOG_ERROR : LOG_GENERAL,
			  "Restored %d of %d users\n", user_count, restored_count);
		ezxml_free (data);
	}
	if (!user_list) {
		flog (LOG_ERROR, "No user data found.  Creating admin user.");
		USER *admin = create_new_user ("admin", "admin");
		if (admin) {
			admin->rank = RANK_ADMINISTRATOR;
		}
	}
	/* Never write the config file until changes are made.
	   If something goes wrong, this reduces chances of
	   clobbering the existing password file with a new one. */
	dirty = false;
}


/* Write XML to a file.  Values alternate literal, value, literal, value...;
   the list must be NULL-terminated.  Values have*/
static void fprintxml (FILE *file, ...) {
	assert (file);
	va_list parameters;
	va_start (parameters, file);
	char *val;
	while ((val = va_arg (parameters, char *))) {
		fprintf (file, "%s", val);
		val = va_arg (parameters, char *);
		if (!val) break;
		while (*val) {
			switch (*val) {
				case '\'':
					fprintf (file, "&apos;");
					break;
				case '"':
					fprintf (file, "&quot;");
					break;
				case '<':
					fprintf (file, "&lt;");
					break;
				case '>':
					fprintf (file, "&gt;");
					break;
				case '&':
					fprintf (file, "&amp;");
					break;
				default:
					putc (*val, file);
			}
			val++;
		}
	}
	va_end (parameters);
}

/* Write the data into the userdata file */
static bool write_users (FILE *out) {
	assert (out);
	USER *user;
	fprintf (out, "<?xml version='1.0' encoding='UTF-8'?>\n"
			 "<pianodpasswd version='1.0'>\n");
	for (user = user_list; user; user = user->next) {
		fprintxml (out, "  <user name='", user->name,
						"' password='", user->password,
						"' level='", user_type_name (user->rank), "'>\n", NULL);
        if (user->pandora_credentials.username) {
            assert (user->pandora_credentials.password);
            fprintxml (out, "    <pandora>\n"
                            "      <user name='", user->pandora_credentials.username,
                            "' password='", user->pandora_credentials.password,
                            "' ownership='", get_manager_rule_by_id
                                    (user->pandora_credentials.manager_rule)->name, "' />\n"
                            "    </pandora>\n", NULL);
        }
		for (int i = 0; i < countof (privileges); i++) {
			if (privileges [i].persistable) {
				fprintxml (out, "    <privilege name='", privileges [i].name,
						   "' granted='", user->privileges [privileges [i].index] ? "true" : "false",
						   "' />\n", NULL);
			}
		}
		persist_station_preferences (out, user);
		fprintf (out, "  </user>\n");
	}
	fprintf (out, "</pianodpasswd>\n");
	return !ferror (out);
}

/* Persist user data to a file.  Write to a new file, then carefully move
   the old one out/new one in in such a way as to minimize risk. */
bool users_persist (const char *filename) {
	if (!dirty) {
		return true;
	}
	assert (filename);
	bool success = false;
	char *newfile = malloc (strlen (filename) + 5);
	char *oldfile = malloc (strlen (filename) + 5);
	if (newfile && oldfile) {
		strcat (strcpy (newfile, filename), "-new");
		strcat (strcpy (oldfile, filename), "-old");
		FILE *out;
		if ((out = fopen (newfile, "w"))) {
			success = write_users (out);
			fclose (out);
			if (success) {
				unlink (oldfile);
				link (filename, oldfile);
				success = (rename (newfile, filename) >= 0);
				if (success) {
					dirty = false;
				} else {
					perror ("rename");
				}
			}
		} else {
			/* Write without making a backup. */
			/* Required when running as nobody with files in /etc. */
			if ((out = fopen (filename, "w"))) {
				success = write_users (out);
				fclose (out);
			} else {
				perror (filename);
			}
		}
	} else {
		perror ("malloc");
	}
	free (newfile);
	free (oldfile);
	return (success);
}

/* Destroy the userlist and free up memory. */
void users_destroy (void) {
	destroy_users (user_list);
	user_list = NULL;
}
