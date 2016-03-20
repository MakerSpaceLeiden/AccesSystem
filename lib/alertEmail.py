import smtplib
import time
import logging
from email.mime.text import MIMEText

import configRead

def send_email(mailmsg,mailsubject, to=None):
  cnf = configRead.cnf

  sendfrom  = cnf['alert_email_from'];
  sendto = [ cnf['alert_email_to'] ];

  # Include extra person if provided
  if to: sendto.append(to)

  msg = MIMEText(mailmsg)

  msg['Subject'] = mailsubject 
  msg['From'] = sendfrom
  msg['To'] = ','.join(sendto)

  smtpcnf = cnf['smtp']

  port = 25
  if 'port' in smtpcnf:
     port = smtpcnf['port']

  s = smtplib.SMTP(smtpcnf['host'], port)

  if 'username' in smtpcnf:
     s.login(smtpcnf['username'], smtpcnf['password'])

  s.sendmail(sendfrom, sendto, msg.as_string())
  s.quit()


