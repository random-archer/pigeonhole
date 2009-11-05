/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

/* Extension subaddress 
 * --------------------
 *
 * Author: Stephan Bosch
 * Specification: RFC 3598
 * Implementation: full, but not fully configurable
 * Status: experimental
 *
 */
 
#include "sieve-common.h"

#include "sieve-code.h"
#include "sieve-address.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-address-parts.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include <stdlib.h>
#include <string.h>

/* 
 * Configuration 
 */

#define SUBADDRESS_DEFAULT_SEP "+"

struct ext_subaddress_config {
	char *separator;
};

/*
 * Forward declarations 
 */

const struct sieve_address_part_def user_address_part;
const struct sieve_address_part_def detail_address_part;

static struct sieve_operand_def subaddress_operand;

/*
 * Extension
 */

static bool ext_subaddress_load
	(const struct sieve_extension *ext, void **context);
static void ext_subaddress_unload
	(const struct sieve_extension *ext);
static bool ext_subaddress_validator_load
	(const struct sieve_extension *ext, struct sieve_validator *validator);

const struct sieve_extension_def subaddress_extension = { 
	"subaddress", 
	ext_subaddress_load, 
	ext_subaddress_unload,
	ext_subaddress_validator_load,
	NULL, NULL, NULL, NULL, NULL,
	SIEVE_EXT_DEFINE_NO_OPERATIONS, 
	SIEVE_EXT_DEFINE_OPERAND(subaddress_operand)
};

static bool ext_subaddress_load
(const struct sieve_extension *ext, void **context)
{
	struct ext_subaddress_config *config;
	const char *sep = sieve_get_setting(ext->svinst, "sieve_subaddress_sep");

	if ( sep == NULL )
		sep = SUBADDRESS_DEFAULT_SEP;

	config = i_new(struct ext_subaddress_config, 1);
	config->separator = i_strdup(sep);

	*context = (void *) config;

	return TRUE;
}

static void ext_subaddress_unload
(const struct sieve_extension *ext)
{
	struct ext_subaddress_config *config =
		(struct ext_subaddress_config *) ext->context;

	i_free(config->separator);
	i_free(config);
}

static bool ext_subaddress_validator_load
(const struct sieve_extension *ext, struct sieve_validator *validator)
{
	sieve_address_part_register(validator, ext, &user_address_part); 
	sieve_address_part_register(validator, ext, &detail_address_part); 

	return TRUE;
}

/*
 * Address parts
 */
 
enum ext_subaddress_address_part {
  SUBADDRESS_USER,
  SUBADDRESS_DETAIL
};

/* Forward declarations */

static const char *subaddress_user_extract_from
	(const struct sieve_address_part *addrp, const struct sieve_address *address);
static const char *subaddress_detail_extract_from
	(const struct sieve_address_part *addrp, const struct sieve_address *address);

/* Address part objects */	

const struct sieve_address_part_def user_address_part = {
	SIEVE_OBJECT("user", &subaddress_operand, SUBADDRESS_USER),
	subaddress_user_extract_from
};

const struct sieve_address_part_def detail_address_part = {
	SIEVE_OBJECT("detail", &subaddress_operand, SUBADDRESS_DETAIL),
	subaddress_detail_extract_from
};

/* Address part implementation */

static const char *subaddress_user_extract_from
(const struct sieve_address_part *addrp, const struct sieve_address *address)
{
	struct ext_subaddress_config *config = 
		(struct ext_subaddress_config *) addrp->object.ext->context;
	const char *sep;

	sep = strstr(address->local_part, config->separator);
	
	if ( sep == NULL ) return address->local_part;
	
	return t_strdup_until(address->local_part, sep);
}

static const char *subaddress_detail_extract_from
(const struct sieve_address_part *addrp, const struct sieve_address *address)
{
	struct ext_subaddress_config *config = 
		(struct ext_subaddress_config *) addrp->object.ext->context;
	const char *sep;

	if ( (sep=strstr(address->local_part, config->separator)) == NULL )
		return NULL; 

	sep += strlen(config->separator);

	/* Just to be sure */
	if ( sep > (address->local_part + strlen(address->local_part)) ) 
		return NULL;

	return sep;
}

/*
 * Operand 
 */

const struct sieve_address_part_def *ext_subaddress_parts[] = {
	&user_address_part, &detail_address_part
};

static const struct sieve_extension_objects ext_address_parts =
	SIEVE_EXT_DEFINE_ADDRESS_PARTS(ext_subaddress_parts);

static struct sieve_operand_def subaddress_operand = { 
	"address-part", 
	&subaddress_extension, 0,
	&sieve_address_part_operand_class,
	&ext_address_parts
};

