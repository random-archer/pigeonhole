#ifndef __SIEVE_ADDRESS_PARTS_H
#define __SIEVE_ADDRESS_PARTS_H

#include "message-address.h"

#include "sieve-common.h"

enum sieve_address_part_code {
	SIEVE_ADDRESS_PART_ALL,
	SIEVE_ADDRESS_PART_LOCAL,
	SIEVE_ADDRESS_PART_DOMAIN,
	SIEVE_ADDRESS_PART_CUSTOM
};

struct sieve_address_part_extension;

struct sieve_address_part {
	const char *identifier;
	
	enum sieve_address_part_code code;
	
	const struct sieve_address_part_extension *extension;
	unsigned int ext_code;

	const char *(*extract_from)(const struct message_address *address);
};

struct sieve_address_part_extension {
	const struct sieve_extension *extension;

	/* Either a single addr-part in this extension ... */
	const struct sieve_address_part *address_part;
	
	/* ... or multiple: then the extension must handle emit/read */
	const struct sieve_address_part *(*get_part)
		(unsigned int code);
};

void sieve_address_parts_link_tags
	(struct sieve_validator *validator, 
		struct sieve_command_registration *cmd_reg,
		unsigned int id_code);
		
void sieve_address_part_register
	(struct sieve_validator *validator, 
	const struct sieve_address_part *addrp, int ext_id);
const struct sieve_address_part *sieve_address_part_find
	(struct sieve_validator *validator, const char *identifier,
		int *ext_id);
void sieve_address_part_extension_set
	(struct sieve_interpreter *interpreter, int ext_id,
		const struct sieve_address_part_extension *ext);

extern const struct sieve_argument address_part_tag;

extern const struct sieve_address_part all_address_part;
extern const struct sieve_address_part local_address_part;
extern const struct sieve_address_part domain_address_part;

extern const struct sieve_address_part *sieve_core_address_parts[];
extern const unsigned int sieve_core_address_parts_count;

const struct sieve_address_part *sieve_opr_address_part_read
  (struct sieve_interpreter *interpreter, 
  	struct sieve_binary *sbin, sieve_size_t *address);
bool sieve_opr_address_part_dump
	(struct sieve_interpreter *interpreter,
		struct sieve_binary *sbin, sieve_size_t *address);

bool sieve_address_match_stringlist
	(const struct sieve_address_part *addrp, const struct sieve_match_type *mtch, 
		const struct sieve_comparator *cmp,	struct sieve_coded_stringlist *key_list,
		const char *data);

#endif /* __SIEVE_ADDRESS_PARTS_H */
