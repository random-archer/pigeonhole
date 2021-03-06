/* Copyright (c) 2002-2017 Pigeonhole authors, see the included COPYING file
 */

/* Notify method mailto
 * --------------------
 *
 * Authors: Stephan Bosch
 * Specification: RFC 5436
 * Implementation: full
 * Status: testing
 *
 */

/* FIXME: URI syntax conforms to something somewhere in between RFC 2368 and
 *   draft-duerst-mailto-bis-05.txt. Should fully migrate to new specification
 *   when it matures. This requires modifications to the address parser (no
 *   whitespace allowed within the address itself) and UTF-8 support will be
 *   required in the URL.
 */

#include "lib.h"
#include "array.h"
#include "str.h"
#include "ioloop.h"
#include "str-sanitize.h"
#include "ostream.h"
#include "message-date.h"
#include "mail-storage.h"

#include "sieve-common.h"
#include "sieve-address.h"
#include "sieve-address-source.h"
#include "sieve-message.h"
#include "sieve-smtp.h"
#include "sieve-settings.h"

#include "sieve-ext-enotify.h"

#include "rfc2822.h"

#include "uri-mailto.h"

/*
 * Configuration
 */

#define NTFY_MAILTO_MAX_RECIPIENTS  8
#define NTFY_MAILTO_MAX_HEADERS     16
#define NTFY_MAILTO_MAX_SUBJECT     256

/*
 * Mailto notification configuration
 */

struct ntfy_mailto_config {
	struct sieve_address_source envelope_from;
};

/*
 * Mailto notification method
 */

static bool ntfy_mailto_load
	(const struct sieve_enotify_method *nmth, void **context);
static void ntfy_mailto_unload
	(const struct sieve_enotify_method *nmth);

static bool ntfy_mailto_compile_check_uri
	(const struct sieve_enotify_env *nenv, const char *uri, const char *uri_body);
static bool ntfy_mailto_compile_check_from
	(const struct sieve_enotify_env *nenv, string_t *from);

static const char *ntfy_mailto_runtime_get_notify_capability
	(const struct sieve_enotify_env *nenv, const char *uri, const char *uri_body,
		const char *capability);
static bool ntfy_mailto_runtime_check_uri
	(const struct sieve_enotify_env *nenv, const char *uri, const char *uri_body);
static bool ntfy_mailto_runtime_check_operands
	(const struct sieve_enotify_env *nenv, const char *uri,const char *uri_body,
		string_t *message, string_t *from, pool_t context_pool,
		void **method_context);

static int ntfy_mailto_action_check_duplicates
	(const struct sieve_enotify_env *nenv,
		const struct sieve_enotify_action *nact,
		const struct sieve_enotify_action *nact_other);

static void ntfy_mailto_action_print
	(const struct sieve_enotify_print_env *penv,
		const struct sieve_enotify_action *nact);

static int ntfy_mailto_action_execute
	(const struct sieve_enotify_exec_env *nenv,
		const struct sieve_enotify_action *nact);

const struct sieve_enotify_method_def mailto_notify = {
	"mailto",
	ntfy_mailto_load,
	ntfy_mailto_unload,
	ntfy_mailto_compile_check_uri,
	NULL,
	ntfy_mailto_compile_check_from,
	NULL,
	ntfy_mailto_runtime_check_uri,
	ntfy_mailto_runtime_get_notify_capability,
	ntfy_mailto_runtime_check_operands,
	NULL,
	ntfy_mailto_action_check_duplicates,
	ntfy_mailto_action_print,
	ntfy_mailto_action_execute
};

/*
 * Reserved and unique headers
 */

static const char *_reserved_headers[] = {
	"auto-submitted",
	"received",
	"message-id",
	"data",
	"bcc",
	"in-reply-to",
	"references",
	"resent-date",
	"resent-from",
	"resent-sender",
	"resent-to",
	"resent-cc",
 	"resent-bcc",
	"resent-msg-id",
	"from",
	"sender",
	NULL
};

static const char *_unique_headers[] = {
	"reply-to",
	NULL
};

/*
 * Method context data
 */

struct ntfy_mailto_context {
	struct uri_mailto *uri;
	const char *from_normalized;
};

/*
 * Method registration
 */

static bool ntfy_mailto_load
(const struct sieve_enotify_method *nmth, void **context)
{
	struct sieve_instance *svinst = nmth->svinst;
	struct ntfy_mailto_config *config;

	if ( *context != NULL ) {
		ntfy_mailto_unload(nmth);
	}

	config = i_new(struct ntfy_mailto_config, 1);

	(void)sieve_address_source_parse_from_setting	(svinst,
		default_pool, "sieve_notify_mailto_envelope_from",
		&config->envelope_from);

	*context = (void *) config;

	return TRUE;
}

static void ntfy_mailto_unload
(const struct sieve_enotify_method *nmth)
{
	struct ntfy_mailto_config *config =
		(struct ntfy_mailto_config *) nmth->context;
	char *address = (char *)config->envelope_from.address;

	i_free(address);
	i_free(config);
}

/*
 * Validation
 */

static bool ntfy_mailto_compile_check_uri
(const struct sieve_enotify_env *nenv, const char *uri ATTR_UNUSED,
	const char *uri_body)
{
	return uri_mailto_validate
		(uri_body, _reserved_headers, _unique_headers,
			NTFY_MAILTO_MAX_RECIPIENTS, NTFY_MAILTO_MAX_HEADERS, nenv->ehandler);
}

static bool ntfy_mailto_compile_check_from
(const struct sieve_enotify_env *nenv, string_t *from)
{
	const char *error;
	bool result = FALSE;

	T_BEGIN {
		result = sieve_address_validate(from, &error);

		if ( !result ) {
			sieve_enotify_error(nenv,
				"specified :from address '%s' is invalid for "
				"the mailto method: %s",
				str_sanitize(str_c(from), 128), error);
		}
	} T_END;

	return result;
}

/*
 * Runtime
 */

static const char *ntfy_mailto_runtime_get_notify_capability
(const struct sieve_enotify_env *nenv ATTR_UNUSED, const char *uri ATTR_UNUSED,
	const char *uri_body, const char *capability)
{
	if ( !uri_mailto_validate(uri_body, _reserved_headers, _unique_headers,
			NTFY_MAILTO_MAX_RECIPIENTS, NTFY_MAILTO_MAX_HEADERS, NULL) ) {
		return NULL;
	}

	if ( strcasecmp(capability, "online") == 0 )
		return "maybe";

	return NULL;
}

static bool ntfy_mailto_runtime_check_uri
(const struct sieve_enotify_env *nenv ATTR_UNUSED, const char *uri ATTR_UNUSED,
	const char *uri_body)
{
	return uri_mailto_validate
		(uri_body, _reserved_headers, _unique_headers,
			NTFY_MAILTO_MAX_RECIPIENTS, NTFY_MAILTO_MAX_HEADERS, NULL);
}

static bool ntfy_mailto_runtime_check_operands
(const struct sieve_enotify_env *nenv, const char *uri ATTR_UNUSED,
	const char *uri_body, string_t *message ATTR_UNUSED, string_t *from,
	pool_t context_pool, void **method_context)
{
	struct ntfy_mailto_context *mtctx;
	struct uri_mailto *parsed_uri;
	const char *error, *normalized;

	/* Need to create context before validation to have arrays present */
	mtctx = p_new(context_pool, struct ntfy_mailto_context, 1);

	/* Validate :from */
	if ( from != NULL ) {
		T_BEGIN {
			normalized = sieve_address_normalize(from, &error);

			if ( normalized == NULL ) {
				sieve_enotify_error(nenv,
					"specified :from address '%s' is invalid for "
					"the mailto method: %s",
					str_sanitize(str_c(from), 128), error);
			} else
				mtctx->from_normalized = p_strdup(context_pool, normalized);
		} T_END;

		if ( normalized == NULL ) return FALSE;
	}

	if ( (parsed_uri=uri_mailto_parse
		(uri_body, context_pool, _reserved_headers,
			_unique_headers, NTFY_MAILTO_MAX_RECIPIENTS, NTFY_MAILTO_MAX_HEADERS,
			nenv->ehandler)) == NULL ) {
		return FALSE;
	}

	mtctx->uri = parsed_uri;
	*method_context = (void *) mtctx;
	return TRUE;
}

/*
 * Action duplicates
 */

static int ntfy_mailto_action_check_duplicates
(const struct sieve_enotify_env *nenv ATTR_UNUSED,
	const struct sieve_enotify_action *nact,
	const struct sieve_enotify_action *nact_other)
{
	struct ntfy_mailto_context *mtctx =
		(struct ntfy_mailto_context *) nact->method_context;
	struct ntfy_mailto_context *mtctx_other =
		(struct ntfy_mailto_context *) nact_other->method_context;
	const struct uri_mailto_recipient *new_rcpts, *old_rcpts;
	unsigned int new_count, old_count, i, j;
	unsigned int del_start = 0, del_len = 0;

	new_rcpts = array_get(&mtctx->uri->recipients, &new_count);
	old_rcpts = array_get(&mtctx_other->uri->recipients, &old_count);

	for ( i = 0; i < new_count; i++ ) {
		for ( j = 0; j < old_count; j++ ) {
			if ( sieve_address_compare
				(new_rcpts[i].normalized, old_rcpts[j].normalized, TRUE) == 0 )
				break;
		}

		if ( j == old_count ) {
			/* Not duplicate */
			if ( del_len > 0 ) {
				/* Perform pending deletion */
				array_delete(&mtctx->uri->recipients, del_start, del_len);

				/* Make sure the loop integrity is maintained */
				i -= del_len;
				new_rcpts = array_get(&mtctx->uri->recipients, &new_count);
			}
			del_len = 0;
		} else {
			/* Mark deletion */
			if ( del_len == 0 )
				del_start = i;
			del_len++;
		}
	}

	/* Perform pending deletion */
	if ( del_len > 0 ) {
		array_delete(&mtctx->uri->recipients, del_start, del_len);
	}

	return ( array_count(&mtctx->uri->recipients) > 0 ? 0 : 1 );
}

/*
 * Action printing
 */

static void ntfy_mailto_action_print
(const struct sieve_enotify_print_env *penv,
	const struct sieve_enotify_action *nact)
{
	unsigned int count, i;
	const struct uri_mailto_recipient *recipients;
	const struct uri_mailto_header_field *headers;
	struct ntfy_mailto_context *mtctx =
		(struct ntfy_mailto_context *) nact->method_context;

	/* Print main method parameters */

	sieve_enotify_method_printf
		(penv,   "    => importance   : %llu\n",
			(unsigned long long)nact->importance);

	if ( nact->message != NULL )
		sieve_enotify_method_printf
			(penv, "    => subject      : %s\n", nact->message);
	else if ( mtctx->uri->subject != NULL )
		sieve_enotify_method_printf
			(penv, "    => subject      : %s\n", mtctx->uri->subject);

	if ( nact->from != NULL )
		sieve_enotify_method_printf
			(penv, "    => from         : %s\n", nact->from);

	/* Print mailto: recipients */

	sieve_enotify_method_printf(penv,   "    => recipients   :\n" );

	recipients = array_get(&mtctx->uri->recipients, &count);
	if ( count == 0 ) {
		sieve_enotify_method_printf(penv,   "       NONE, action has no effect\n");
	} else {
		for ( i = 0; i < count; i++ ) {
			if ( recipients[i].carbon_copy )
				sieve_enotify_method_printf
					(penv,   "       + Cc: %s\n", recipients[i].full);
			else
				sieve_enotify_method_printf
					(penv,   "       + To: %s\n", recipients[i].full);
		}
	}

	/* Print accepted headers for notification message */

	headers = array_get(&mtctx->uri->headers, &count);
	if ( count > 0 ) {
		sieve_enotify_method_printf(penv,   "    => headers      :\n" );
		for ( i = 0; i < count; i++ ) {
			sieve_enotify_method_printf(penv,   "       + %s: %s\n",
				headers[i].name, headers[i].body);
		}
	}

	/* Print body for notification message */

	if ( mtctx->uri->body != NULL )
		sieve_enotify_method_printf
			(penv, "    => body         : \n--\n%s\n--\n", mtctx->uri->body);

	/* Finish output with an empty line */

	sieve_enotify_method_printf(penv,   "\n");
}

/*
 * Action execution
 */

static bool _contains_8bit(const char *msg)
{
	const unsigned char *s = (const unsigned char *)msg;

	for (; *s != '\0'; s++) {
		if ((*s & 0x80) != 0)
			return TRUE;
	}

	return FALSE;
}

static int ntfy_mailto_send
(const struct sieve_enotify_exec_env *nenv,
	const struct sieve_enotify_action *nact,
	const char *owner_email)
{
	struct sieve_instance *svinst = nenv->svinst;
	const struct sieve_message_data *msgdata = nenv->msgdata;
	const struct sieve_script_env *senv = nenv->scriptenv;
	struct ntfy_mailto_context *mtctx =
		(struct ntfy_mailto_context *) nact->method_context;
	struct ntfy_mailto_config *mth_config =
		(struct ntfy_mailto_config *)nenv->method->context;
	struct sieve_address_source env_from =
		mth_config->envelope_from;
	const char *from = NULL, *from_smtp = NULL;
	const char *subject = mtctx->uri->subject;
	const char *body = mtctx->uri->body;
	string_t *to, *cc, *all;
	const struct uri_mailto_recipient *recipients;
	const struct uri_mailto_header_field *headers;
	struct sieve_smtp_context *sctx;
	struct ostream *output;
	string_t *msg;
	unsigned int count, i, hcount, h;
	const char *outmsgid, *error;
	int ret;

	/* Get recipients */
	recipients = array_get(&mtctx->uri->recipients, &count);
	if ( count == 0  ) {
		sieve_enotify_warning(nenv,
			"notify mailto uri specifies no recipients; action has no effect");
		return 0;
	}

	/* Just to be sure */
	if ( !sieve_smtp_available(senv) ) {
		sieve_enotify_global_warning(nenv,
			"notify mailto method has no means to send mail");
		return 0;
	}

	/* Determine which sender to use

	   From RFC 5436, Section 2.3:

		 The ":from" tag overrides the default sender of the notification
		 message.  "Sender", here, refers to the value used in the [RFC5322]
		 "From" header.  Implementations MAY also use this value in the
		 [RFC5321] "MAIL FROM" command (the "envelope sender"), or they may
		 prefer to establish a mailbox that receives bounces from notification
		 messages.
	 */
	if ( (nenv->flags & SIEVE_EXECUTE_FLAG_NO_ENVELOPE) == 0 ) {
		from_smtp = sieve_message_get_sender(nenv->msgctx);
		if ( from_smtp == NULL ) {
			/* "<>" */
			i_zero(&env_from);
			env_from.type = SIEVE_ADDRESS_SOURCE_EXPLICIT;
		}
	}
	if ( (ret=sieve_address_source_get_address(&env_from, svinst,
		senv, nenv->msgctx, nenv->flags, &from_smtp)) < 0 ) {
		from_smtp = NULL;
	} else if ( ret == 0 ) {
		if ( mtctx->from_normalized != NULL )
			from_smtp = mtctx->from_normalized;
		else if ( svinst->user_email != NULL )
			from_smtp = sieve_address_to_string(svinst->user_email);
		else
			from_smtp = sieve_get_postmaster_address(senv);
	}

	/* Determine message from address */
	if ( nact->from == NULL ) {
		from = t_strdup_printf("<%s>", from_smtp);
	} else {
		from = nact->from;
	}

	/* Determine subject */
	if ( nact->message != NULL ) {
		/* FIXME: handle UTF-8 */
		subject = str_sanitize(nact->message, NTFY_MAILTO_MAX_SUBJECT);
	} else if ( subject == NULL ) {
		const char *const *hsubject;

		/* Fetch subject from original message */
		if ( mail_get_headers_utf8
			(msgdata->mail, "subject", &hsubject) > 0 )
			subject = str_sanitize(t_strdup_printf("Notification: %s", hsubject[0]),
				NTFY_MAILTO_MAX_SUBJECT);
		else
			subject = "Notification: (no subject)";
	}

	/* Compose To and Cc headers */
	to = NULL;
	cc = NULL;
	all = t_str_new(256);
	for ( i = 0; i < count; i++ ) {
		if ( recipients[i].carbon_copy ) {
			if ( cc == NULL ) {
				cc = t_str_new(256);
				str_append(cc, recipients[i].full);
			} else {
				str_append(cc, ", ");
				str_append(cc, recipients[i].full);
			}
		} else {
			if ( to == NULL ) {
				to = t_str_new(256);
				str_append(to, recipients[i].full);
			} else {
				str_append(to, ", ");
				str_append(to, recipients[i].full);
			}
		}
		if ( i < 3) {
			if ( i > 0 )
				str_append(all, ", ");
			str_append_c(all, '<');
			str_append(all, str_sanitize(recipients[i].normalized, 256));
			str_append_c(all, '>');
		} else if (i == 3) {
			str_printfa(all, ", ... (%u total)", count);
		}
	}

	msg = t_str_new(512);
	outmsgid = sieve_message_get_new_id(svinst);

	rfc2822_header_write(msg, "X-Sieve", SIEVE_IMPLEMENTATION);
	rfc2822_header_write(msg, "Message-ID", outmsgid);
	rfc2822_header_write(msg, "Date", message_date_create(ioloop_time));
	rfc2822_header_utf8_printf(msg, "Subject", "%s", subject);

	rfc2822_header_utf8_printf(msg, "From", "%s", from);

	if ( to != NULL )
		rfc2822_header_utf8_printf(msg, "To", "%s", str_c(to));

	if ( cc != NULL )
		rfc2822_header_utf8_printf(msg, "Cc", "%s", str_c(cc));

	rfc2822_header_printf(msg, "Auto-Submitted",
		"auto-notified; owner-email=\"%s\"", owner_email);
	rfc2822_header_write(msg, "Precedence", "bulk");

	/* Set importance */
	switch ( nact->importance ) {
	case 1:
		rfc2822_header_write(msg, "X-Priority", "1 (Highest)");
		rfc2822_header_write(msg, "Importance", "High");
		break;
	case 3:
		rfc2822_header_write(msg, "X-Priority", "5 (Lowest)");
		rfc2822_header_write(msg, "Importance", "Low");
		break;
	case 2:
	default:
		rfc2822_header_write(msg, "X-Priority", "3 (Normal)");
		rfc2822_header_write(msg, "Importance", "Normal");
		break;
	}

	/* Add custom headers */

	headers = array_get(&mtctx->uri->headers, &hcount);
	for ( h = 0; h < hcount; h++ ) {
		const char *name = rfc2822_header_field_name_sanitize(headers[h].name);

		rfc2822_header_write(msg, name, headers[h].body);
	}

	/* Generate message body */

	rfc2822_header_write(msg, "MIME-Version", "1.0");
	if ( body != NULL ) {
		if (_contains_8bit(body)) {
			rfc2822_header_write
				(msg, "Content-Type", "text/plain; charset=utf-8");
			rfc2822_header_write(msg, "Content-Transfer-Encoding", "8bit");
		} else {
			rfc2822_header_write
				(msg, "Content-Type", "text/plain; charset=us-ascii");
			rfc2822_header_write(msg, "Content-Transfer-Encoding", "7bit");
		}
		str_printfa(msg, "\r\n%s\r\n", body);

	} else {
		rfc2822_header_write
			(msg, "Content-Type", "text/plain; charset=US-ASCII");
		rfc2822_header_write(msg, "Content-Transfer-Encoding", "7bit");

		str_append(msg, "\r\nNotification of new message.\r\n");
	}

	sctx = sieve_smtp_start(senv, from_smtp);

	/* Send message to all recipients */
	for ( i = 0; i < count; i++ )
		sieve_smtp_add_rcpt(sctx, recipients[i].normalized);

	output = sieve_smtp_send(sctx);
	o_stream_nsend(output, str_data(msg), str_len(msg));

	if ( (ret=sieve_smtp_finish(sctx, &error)) <= 0 ) {
		if (ret < 0)  {
			sieve_enotify_global_error(nenv,
				"failed to send mail notification to %s: %s (temporary failure)",
				str_c(all),	str_sanitize(error, 512));
		} else {
			sieve_enotify_global_log_error(nenv,
				"failed to send mail notification to %s: %s (permanent failure)",
				str_c(all),	str_sanitize(error, 512));
		}
	} else {
		sieve_enotify_global_info(nenv,
			"sent mail notification to %s", str_c(all));
	}

	return 0;
}

static int ntfy_mailto_action_execute
(const struct sieve_enotify_exec_env *nenv,
	const struct sieve_enotify_action *nact)
{
	struct sieve_instance *svinst = nenv->svinst;
	const struct sieve_script_env *senv = nenv->scriptenv;
	struct mail *mail = nenv->msgdata->mail;
	const char *owner_email;
	const char *const *hdsp;
	int ret;

	owner_email = sieve_address_to_string(svinst->user_email);
	if ( owner_email == NULL &&
		(nenv->flags & SIEVE_EXECUTE_FLAG_NO_ENVELOPE) == 0 )
		owner_email = sieve_message_get_final_recipient(nenv->msgctx);
	if ( owner_email == NULL )
		owner_email = sieve_get_postmaster_address(senv);
	i_assert( owner_email != NULL );

	/* Is the message an automatic reply ? */
	if ( (ret=mail_get_headers(mail, "auto-submitted", &hdsp)) < 0 ) {
		sieve_enotify_critical(nenv,
			"mailto notification: "
				"failed to read `auto-submitted' header field",
			"mailto notification: "
				"failed to read `auto-submitted' header field: %s",
			mailbox_get_last_error(mail->box, NULL));
		return -1;
	}

	/* Theoretically multiple headers could exist, so lets make sure */
	if ( ret > 0 ) {
		while ( *hdsp != NULL ) {
			if ( strcasecmp(*hdsp, "no") != 0 ) {
				const char *from = NULL;

				if ( (nenv->flags & SIEVE_EXECUTE_FLAG_NO_ENVELOPE) == 0 )
					from = sieve_message_get_sender(nenv->msgctx);
				from = (from == NULL ? "" :
					t_strdup_printf(" from <%s>", str_sanitize(from, 256)));

				sieve_enotify_global_info(nenv,
					"not sending notification "
					"for auto-submitted message%s", from);
				return 0;
			}
			hdsp++;
		}
	}

	T_BEGIN {
		ret = ntfy_mailto_send(nenv, nact, owner_email);
	} T_END;

	return ret;
}




