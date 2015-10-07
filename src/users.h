/*
 *  users.h
 *  pianod
 *
 *  Created by Perette Barella on 2012-03-20.
 *  Copyright 2012-2014 Devious Fish. All rights reserved.
 *
 */

#ifndef _USERS_H
#define _USERS_H

#include <stdbool.h>
#include <fb_public.h>
#include <piano.h>

#include "event.h"

typedef struct user_t USER;

/* User types in ascending ranks/level of authority */
typedef enum user_type_t {
	RANK_NONE,
	RANK_LISTENER,
	RANK_STANDARD,
	RANK_ADMINISTRATOR
} USER_RANK;

typedef enum user_privileges_t {
	PRIVILEGE_MANAGER,
	PRIVILEGE_SERVICE,
	PRIVILEGE_INFLUENCE,
	PRIVILEGE_TUNER,
	ATTRIBUTE_PRESENT,
	PRIVILEGE_COUNT
} PRIVILEGE;

typedef struct user_context_t {
	USER *user;
	char *search_term;
	PianoSearchResult_t *search_results;
	WAIT_EVENT waiting_for;
} USER_CONTEXT;

typedef enum manager_rule_t {
	MANAGER_ADMINISTRATOR,
	MANAGER_NONE,
	MANAGER_USER
} MANAGER_RULE;

typedef struct pandora_credentials_t {
	char *username;
	char *password;
	USER *manager; /* User who can revise station */
    USER *creator; /* User with whom to persist info */
	MANAGER_RULE manager_rule;
} CREDENTIALS;

typedef enum send_users_t {
    /* Lower values are PRIVILEGE_* values */
    SEND_USERS_ONLINE = PRIVILEGE_COUNT,
    SEND_USERS_AUTOTUNED,
    SEND_USERS_REMEMBERING_CREDENTIALS,
    SEND_USERS_COUNT
} SEND_USERS;

extern USER_RANK get_rank_by_name (const char *name);
extern PRIVILEGE get_privilege_id_by_name (const char *name);
extern const char *user_type_name (USER_RANK rank);

extern USER_RANK get_effective_rank (struct user_t *user);
extern bool have_rank (struct user_t *user, USER_RANK minimum);
extern bool have_privilege (struct user_t *user, PRIVILEGE priv);

extern void set_visitor_rank (USER_RANK rank);
extern void set_ownership_rule (MANAGER_RULE rule, USER *owner);
extern void set_rank (struct user_t *user, USER_RANK level);
extern void set_privilege (struct user_t *user, PRIVILEGE priv, bool enabled);
extern void announce_privileges (FB_SERVICE *service, struct user_t *user);

extern struct user_t *get_first_user (void);
extern struct user_t *get_next_user (struct user_t *user);
extern struct user_t *get_user_by_name (FB_EVENT *event, const char *username);
extern struct user_t *authenticate_user (const char *username, const char *password);
extern struct user_t *get_startscript_user (void);
extern struct user_t *create_new_user (char *username, char *password);
extern bool change_password (USER *user, const char *old, const char *password);
extern bool set_user_password (USER *user, const char *password);
extern void delete_user (USER *user);

extern bool is_user_online (FB_SERVICE *service, struct user_t *user);
extern bool valid_user_list (FB_EVENT *event, char * const*username);
extern void clear_privilege (PRIVILEGE priv);
extern void set_privileges (char * const*username, PRIVILEGE priv, bool setting);
extern void send_privileges (FB_EVENT *event, struct user_t *user);
extern void save_pandora_credentials (CREDENTIALS *creds);
extern bool restore_pandora_credentials (USER *username, CREDENTIALS *creds);

extern void send_user_list (FB_EVENT *event, const char *who);
/* which parameter can be a value from: AUTOTUNE_MODE, PRIVILEGE, or a SEND_USER value. */
extern void send_select_users (FB_SERVICE *service, FB_EVENT *event,
                               int which, bool details);
extern void user_logoff (FB_SERVICE *service, struct user_t *user, const char *message);
extern const char *get_user_name (struct user_t *user);

extern struct station_preferences_t *get_station_preferences (struct user_t *user);
extern void set_station_preferences (struct user_t *user, struct station_preferences_t *prefs);

extern void destroy_pandora_credentials (CREDENTIALS *creds);
extern void users_restore (const char *filename);
extern bool users_persist (const char *filename);
extern void users_destroy (void);


#endif /* __USERS_H__ */
