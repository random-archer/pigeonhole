/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#ifndef __TESTSUITE_MAILSTORE_H
#define __TESTSUITE_MAILSTORE_H

#include "lib.h"
#include "mail-storage.h"

#include "sieve-common.h" 

/*
 * Initialization
 */

void testsuite_mailstore_init
	(const char *user, const char *home, struct mail_user *service_user);
void testsuite_mailstore_deinit(void);
void testsuite_mailstore_reset(void);

/* 
 * Namespace
 */

struct mail_namespace *testsuite_mailstore_get_namespace(void);

/*
 * Mailbox Access
 */

bool testsuite_mailstore_mailbox_create
	(const struct sieve_runtime_env *renv ATTR_UNUSED, const char *folder);

bool testsuite_mailstore_mail_index
	(const struct sieve_runtime_env *renv, const char *folder, 
		unsigned int index);

#endif /* __TESTSUITE_MAILSTORE */