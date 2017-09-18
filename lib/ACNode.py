#!/usr/bin/env python
#
import SharedSecret as SharedSecret
import TrustOnFirstContact as TrustOnFirstContact
import Beat as Beat
import ACNodeBase as ACNodeBase

# Protocol 0.0
#class ACNode(ACNodeBase.ACNodeBase):
#    pass

# Protocol SIG/1
# class ACNode(SharedSecret.SharedSecret):
#    pass

# Protocol SIG/1 and /2
class ACNode(TrustOnFirstContact.TrustOnFirstContact, SharedSecret.SharedSecret):
   pass

