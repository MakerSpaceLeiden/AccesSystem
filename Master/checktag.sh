#!/bin/sh
if [ $# != 1 ]; then
	echo "Syntax: $0 <tag>"
	exit 1
fi
TOK=`cat /etc/crm_uk_bearer_secret.txt | head -1`
curl -H "X-Bearer: $TOK " -F tag=$1  https://makerspaceleiden.nl/crm/acl/api/v1/getok/spacedeur
