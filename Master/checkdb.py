import mysql.connector

SQL ="""select active, members_tag.owner_id, first_name,last_name,email,members_tag.id from acl_entitlement 
            inner join acl_machine on acl_machine.node_machine_name = %s and acl_machine.requires_permit_id = acl_entitlement.permit_id 
                    inner join members_tag on members_tag.tag = %s and acl_entitlement.holder_id = members_tag.owner_id 
                            inner join members_user on members_user.id = members_tag.owner_id"""
SQL2 ="""select active, members_tag.owner_id, first_name,last_name,email, members_tag.id  from acl_entitlement 
            inner join acl_machine on acl_machine.node_machine_name = %s and acl_machine.requires_permit_id = acl_entitlement.permit_id 
                    inner join members_tag on members_tag.tag like %s and acl_entitlement.holder_id = members_tag.owner_id 
                            inner join members_user on members_user.id = members_tag.owner_id"""

SQL3 = """select owner_id, first_name, last_name, email, members_tag.id from members_user, members_tag 
                          where ( members_tag.tag = %s or members_tag.tag = %s ) and members_tag.owner_id = members_user.id """

cnx = mysql.connector.connect(option_files='/usr/local/makerspaceleiden-crm/makerspaceleiden/my.cnf')
cnx.autocommit = True

cursor = cnx.cursor()
for sql in [ SQL, SQL2, SQL3 ]:
  print(sql)
  cursor.execute(sql, ('byebye','4-225-254-122-220-63-128'))
  for line in cursor.fetchall():
    print(line)

