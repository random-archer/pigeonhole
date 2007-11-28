#ifndef __SIEVE_ACTIONS_H
#define __SIEVE_ACTIONS_H

#include "lib.h"
#include "mail-storage.h"

#include "sieve-common.h"

struct sieve_action_exec_env { 
	struct sieve_result *result;
	const struct sieve_message_data *msgdata;
	const struct sieve_mail_environment *mailenv;
};

struct sieve_action {
	const char *name;

	bool (*check_duplicate)	
		(const struct sieve_runtime_env *renv,
			const struct sieve_action *action, void *context1, void *context2);	
	bool (*check_conflict)
		(const struct sieve_runtime_env *renv,
			const struct sieve_action *action1, const struct sieve_action *action2,
			void *context1);

	void (*print)
		(const struct sieve_action *action, void *context);	
		
	bool (*start)
		(const struct sieve_action *action, 
			const struct sieve_action_exec_env *aenv, void *context, 
			void **tr_context);		
	bool (*execute)
		(const struct sieve_action *action, 
			const struct sieve_action_exec_env *aenv, void *tr_context);
	bool (*commit)
		(const struct sieve_action *action, 
			const struct sieve_action_exec_env *aenv, void *tr_context);
	void (*rollback)
		(const struct sieve_action *action, 
			const struct sieve_action_exec_env *aenv, void *tr_context, bool success);
};

struct sieve_side_effect {
	const char *name;
	const struct sieve_action *to_action;
	
	bool (*pre_execute)
		(const struct sieve_side_effect *seffect, const struct sieve_action *action, 
			const struct sieve_action_exec_env *aenv, void **se_context, 
			void *tr_context);
	bool (*post_execute)
		(const struct sieve_side_effect *seffect, const struct sieve_action *action, 
			const struct sieve_action_exec_env *aenv, void *se_context, 
			void *tr_context);
	bool (*post_commit)
		(const struct sieve_side_effect *seffect, const struct sieve_action *action, 
			const struct sieve_action_exec_env *aenv, void *se_context,
			void *tr_context);
	void (*rollback)
		(const struct sieve_side_effect *seffect, const struct sieve_action *action, 
			const struct sieve_action_exec_env *aenv, void *se_context,
			void *tr_context, bool success);
};

/* Actions common to multiple commands */

const struct sieve_action act_store;

struct act_store_context {
	const char *folder;
};

struct act_store_transaction {
	struct act_store_context *context;
	struct mail_namespace *namespace;
	struct mailbox *box;
	struct mailbox_transaction_context *mail_trans;
	const char *error;
};

bool sieve_act_store_add_to_result
	(const struct sieve_runtime_env *renv, const char *folder);

		
#endif /* __SIEVE_ACTIONS_H */
