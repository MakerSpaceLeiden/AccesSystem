#!/usr/bin/env python3.4
#
# Run as
#   python3.4 db.py --dbfile ./sample-keydb.txt 
# to get a dump of the current DB as parsed.
#

import os
import sys
import time
import signal

sys.path.append('../lib')
import ACNode as ACNode

class TextDB(ACNode.ACNode):
  default_dbfile="./dbfile"

  def parseArguments(self):
    self.parser.add('--dbfile',default=self.default_dbfile,
         help='File with access database (default: '+self.default_dbfile+')')

    super().parseArguments()

  def setup(self):
    super().setup()

    self.reload_db()
    signal.signal(signal.SIGHUP, self.reload_db)

  def reload_db(self):
     newuserdb = {}
     try:
       for row in open(self.cnf.dbfile,'r'):
          if row.startswith('#'): continue
          if len(row.split(':')) != 4: continue
          
          # Parse keys to valid input
          tag,access,name,email = row.strip().split(':')
          tag = '-'.join(map(str.strip, tag.split(',')))
          
          # Break access up into the things this user
          # has access to.          
          allowed_items = access.split(',')

          # Create database
          newuserdb[tag] = { 'tag': 'hidden', 'access': allowed_items, 'name': name, 'email': email }
     except IOError as e:
       self.logger.critical("I/O error %s", e.strerror())
       sys.exit(1)
     except ValueError:
       self.logger.error("Could not convert data to an integer -- some malformed tag ? ignored. ")
       raise
     except:
       self.logger.critical("Unexpected error: %s", sys.exc_info()[0])
       sys.exit(1)

     self.logger.info("Reloaded userdb")
     self.userdb = newuserdb

if __name__ == "__main__":
   
   acnode = TextDB('DB','/dev/null')
   acnode.parseArguments()
   acnode.setup()
  
   print(acnode.userdb)

