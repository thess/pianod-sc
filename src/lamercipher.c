/*
 * lamercipher.c
 * pianod
 *
 * Created by Perette Barella on 2014-03-11.
 * Copyright 2014 Devious Fish. All rights reserved.
 *
 */

/*
This is a simplistic cipher intended to be difficult
enough to be inconvenience hand deciphering, but nothing
truly secure.  In short: Enough to keep the riff-raff out.

Usernames cannot be changed in pianod.  The user's name
will be used to generate the key by feeding it through
a 32-bit CRC generator.  Note that a 32-bit generator
yields 31 bits of CRC.  We will then encipher the password
symetrically with the key/CRC as follows:

For each character, starting at the beginning:
    XOR the character with the least significant bits of
    the CRC as follows:
		0x01–0x1f (000x xxxx): Assert(0), or do nothing.
		0x20–0x3f (001x xxxx): XOR 5 bits
        0x40–0x7f (01xx xxxx): XOR 6 bits
        0x80–0xff (1xxx xxxx), XOR 7 bits of the CRC.
    Rotate right the CRC by the number of bits "consumed".

This is certainly not NSA quality, and there are hazards of
inventing your own encryption system; but given the inherent
insecurity of anyone being able to strategically add
    printf ("%s's password is %s\n", user->password, user->name)
and recompiling, this is adequate.  It is, after all,
only a music server.
*/

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

typedef uint32_t lamerkey_t;

static const lamerkey_t lamer_crc_generator = 0xae060ed2 ; /* a/k/a divisor */
static const int lamer_keybits = sizeof (lamerkey_t) * 8 - 1;
/* Thank random.org for our generator CRC. */

extern char *lamer_cipher (const char *keystr, const char *item);


static lamerkey_t compute_crc (const char *data, lamerkey_t divisor, lamerkey_t remainder) {
    lamerkey_t key = 0;
#ifdef VARIABLE_SIZE_DIVISORS
    lamerkey_t divisor_flag = 1;
    lamerkey_t find_high_bit = divisor;
    /* Grab the high bit from the generator, which will tell us when to apply divisor */
    for (divisor_flag = 1; divisor_flag ^ find_high_bit; divisor_flag <<= 1) {
        if (divisor_flag & find_high_bit) {
            find_high_bit ^= divisor_flag;
        }
    }
#else
    lamerkey_t divisor_flag = 1 << (lamer_keybits);
    /* High bit of generator must be set */
    assert (lamer_crc_generator & divisor_flag);
#endif
    /* Loop through the incoming bytes and bits, applying CRC math. */
    for (const char *in = data; *in; in++) {
        for (lamerkey_t bit = 0x80; bit; bit >>=1) {
            key <<= 1;
            if (*in & bit) {
                key |= 1;
            }
            if (key & divisor_flag) {
                key ^= divisor;
            }
        }
    }
    /* Apply the remainder */
    for (lamerkey_t bit = divisor_flag >> 1; bit; bit >>= 1) {
        key <<= 1;
        if (remainder & bit) {
            key |= 1;
        }
        if (key & divisor_flag) {
            key ^= divisor;
        }
    }
#ifdef UNIT_TEST
    /* Remainder should be symmetrical with the key, but beware infinitely recursing */
    static int recursing = 0;
    if (remainder == 0 && !recursing) {
        recursing = 1; /* Avoid rare possibility that key is 0 and infinite recursion */
        assert (compute_crc (data, divisor, key) == remainder);
    }
    recursing = 0;
#endif
    return (key);
}

static lamerkey_t create_key_from_string (const char *source){
    return (compute_crc (source, lamer_crc_generator, 0));
}



/* Encipher or decipher an item based on a key.
   Returns NULL on error.  Original strings are unmodified. */
char *lamer_cipher (const char *keystr, const char *item) {
    char *output = strdup (item);
    if (output) {
        lamerkey_t key = create_key_from_string (keystr);
        char *out = output;
        for (const char *in = item; *in; in++, out++) {
            int bits;
            if ((*in & 0xe0) == 0x20) {
                bits = 5;
            } else if ((*in & 0xc0) == 0x40) {
                bits = 6;
            } else if ((*in & 0x80) == 0x80) {
                bits = 7;
            } else {
                /* We canna encrypt this character, Cap'n. */
                assert (0);
                continue;
            }
            lamerkey_t mask = key & ((1 << bits) - 1);
            *out = (*in ^ mask);
            key = (key >> bits) | (mask << (lamer_keybits - bits));
        }
    }
    return output;
}

#ifdef UNIT_TEST

int main (int argc, char *argv[]) {
    assert (argc > 2);
    char *key = argv [1];
    char *start = argv [2];

    printf ("Running: %s '%s' '%s'\n", argv[0], key, start);
    lamerkey_t crc = compute_crc (start, lamer_crc_generator, 0);
    assert (!*start || crc); // Very rarely, that could actually be the right answer.
    assert (compute_crc (start, lamer_crc_generator, crc) == 0);
    printf ("'%s' CRC: %08x\n", start, crc);

    crc = compute_crc (key, lamer_crc_generator, 0);
    assert (!*key || crc); // Very rarely, that could actually be the right answer.
    assert (compute_crc (key, lamer_crc_generator, crc) == 0);
    printf ("'%s' CRC: %08x\n", key, crc);

    char *encrypted = lamer_cipher (key, start);
    char *decrypted = lamer_cipher (key, encrypted);
    if (strcmp (decrypted, start) != 0) {
        printf ("Fail: start='%s', decrypted='%s'\n", start, decrypted);
        return 1;
    }
    assert (crc == 0 || strlen (encrypted) <= 1 || strcmp (encrypted, decrypted) != 0);
    printf ("'%s' enciphered is\n'%s'\n", start, encrypted);
    return 0;
}

#endif
