#!/usr/bin/env python
#
import SharedSecret as SharedSecret
import TrustOnFirstContact as TrustOnFirstContact
import ACNodeBase

#class ACNode(ACNodeBase.ACNodeBase):
#    pass

# class ACNode(SharedSecret.SharedSecret):
#    pass

# class ACNode(TrustOnFirstContact.TrustOnFirstContact):
#   pass

#class ACNode(TrustOnFirstContact.TrustOnFirstContact, SharedSecret.SharedSecret):
#   pass

class ACNode(SharedSecret.SharedSecret, TrustOnFirstContact.TrustOnFirstContact):
    pass
