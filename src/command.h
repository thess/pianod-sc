/*
 *  service.c
 *  Server-style UI for libpiano.
 *
 *  Created by Perette Barella on 2012-03-10.
 *  Copyright 2012-2014 Devious Fish. All rights reserved.
 *  Parts liberally plagiarized and adapted from PianoBar
 *  Copyright (c) 2008-2011 Lars-Dominik Braun <lars@6xq.net>
 *
 */

#include <fb_public.h>
#include "pianod.h"

#ifndef _COMMAND_H
#define _COMMAND_H

typedef enum my_commands_t {
	NOP = 1,
	TIMESTATUS,
	HELP,
	QUERYSTATUS,
	QUERYHISTORY,
	QUERYQUEUE,
	NEXTSONG,
	PAUSEPLAYBACK,
	STOPPLAYBACK,
	PLAY,
	PLAYPAUSE,
	PLAYSTATION,
	PLAYQUICKMIX,
	QUICKMIXINCLUDED,
	QUICKMIXEXCLUDED,
	QUICKMIXSET,
	QUICKMIXADD,
	QUICKMIXDROP,
	QUICKMIXTOGGLE,
	SELECTQUICKMIX,
	SELECTSTATION,
	STATIONLIST,
	STATIONRATINGS,
	STATIONRATE,
	AUTOTUNESETMODE,
	AUTOTUNEGETMODE,
	AUTOTUNEUSERS,
    AUTOTUNEUSERSLIST,
	AUTOTUNEADDREMOVE,
	/* Owner-privilege commands get a range ease managing privilege. */
	OWNER_RANGE_START,
		STATIONINFO,
		STATIONRENAME,
		STATIONDELETE,
		STATIONCREATEBYSONG,
		STATIONCREATEBYSONGWNAME,
		STATIONCREATE,
		STATIONCREATEWNAME,
		STATIONCREATEBYID,
		STATIONCREATEBYIDWNAME,
		GETSUGGESTIONS,
		RATELOVE,
		RATEHATE,
		RATENEUTRAL,
		RATEOVERPLAYED,
		SEEDADD,
		SEEDADDWSTATION,
		SEEDDELETEBYSONG,
		SEEDDELETEBYID,
		SEEDADDBYSONG,
		SEEDADDBYSONGWSTATION,
		EXPLAINSONGCHOICE,
		CREATEBOOKMARK,
	OWNER_RANGE_END,
	GETVOLUME,
	SETVOLUME,
	GETPROXY,
	SETPROXY,
	GETCONTROLPROXY,
	SETCONTROLPROXY,
	GETHISTORYSIZE,
	SETHISTORYSIZE,
	GETAUDIOQUALITY,
	SETAUDIOQUALITY,
#if defined(ENABLE_CAPTURE)
	GETCAPTUREPATH,
	SETCAPTUREPATH,
#endif
#if defined(ENABLE_SHOUT)
	SETSHOUTCAST,
#endif
	GETRPCHOST,
	SETRPCHOST,
	GETRPCTLSPORT,
	SETRPCTLSPORT,
	GETPARTNER,
	SETPARTNER,
	GETPANDORADEVICE,
	SETPANDORADEVICE,
	GETENCRYPTPASSWORD,
	SETENCRYPTPASSWORD,
	GETDECRYPTPASSWORD,
	SETDECRYPTPASSWORD,
	GETTLSFINGERPRINT,
	SETTLSFINGERPRINT,
	GETOUTPUTDRIVER,
	SETOUTPUTDRIVER,
	GETOUTPUTDEVICE,
	SETOUTPUTDEVICE,
	GETOUTPUTID,
	SETOUTPUTID,
	GETOUTPUTSERVER,
	SETOUTPUTSERVER,
	TESTAUDIOOUTPUT,
	SETLOGGINGFLAGS,
	SHOWUSERACTIONS,
	GETVISITORRANK,
	SETVISITORRANK,
	GETPAUSETIMEOUT,
	SETPAUSETIMEOUT,
	GETPLAYLISTTIMEOUT,
	SETPLAYLISTTIMEOUT,
	GETPANDORARETRY,
	SETPANDORARETRY,
	GETUSERRANK,
	GETPANDORAUSER,
	PANDORAUSER,
	PANDORAUSERSPECIFY,
    PANDORAEXISTING,
	WAITFORAUTHENTICATION,
	WAITFORENDOFSONG,
	WAITFORNEXTSONG,
	AUTHENTICATE,
	AUTHANDEXEC,
	SETMYPASSWORD,
	USERCREATE,
	USERSETPASSWORD,
	USERSETRANK,
	USERDELETE,
	USERGRANT,
	USERREVOKE,
    USERLISTBYPRIVILEGE,
	USERLIST,
    USERLISTPANDORA,
	USERKICK,
	USERKICKVISITORS,
	USERSONLINE,
	YELL,
	SHUTDOWN,
	QUIT,
} COMMAND;

extern void execute_command (APPSTATE *app, FB_EVENT *event);
extern bool init_parser (APPSTATE *app);

#endif /* __COMMAND_H__ */
