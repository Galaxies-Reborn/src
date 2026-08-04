on sqlerror exit failure rollback;

-- Publish 14 uses only the first nine current attributes and the next nine
-- maximum attributes.  The former NGE attribute slots 18-26 are therefore
-- retired in this branch and become the persistent nine-attribute wound set.
update creature_objects
set
	attribute_18 = 0,
	attribute_19 = 0,
	attribute_20 = 0,
	attribute_21 = 0,
	attribute_22 = 0,
	attribute_23 = 0,
	attribute_24 = 0,
	attribute_25 = 0,
	attribute_26 = 0;

update version_number set version_number=272, min_version_number=272;

commit;
