import ed25519 as ed25519
import base64

payload="SIG/2.0 EknIvgpYsGJw2MgNIrV8KqvUbVkLV65KjtO366xCCGeoiB0iJ8lqdw6YN5CiTfCOR2D95N2Z+63iXBydz7G6Bw== 001506003212 welcome 10.11.0.158 qqg/HXur/fs/ri5Y/LMlwvKk4P711lg6GiVGb86Rkak= SPfYuHlmkccwlL6A7oKjCCvBV6ftRf2gJc0xVmTbuyI="
proto, signature, beat, cmd, ip, pubkey, signkey = payload.split(' ')
proto, signature, signed_payload = payload.split(' ', 2)

publickey = ed25519.VerifyingKey(pubkey, encoding="base64")

try:
	publickey.verify(signature, signed_payload.encode('ASCII'), encoding="base64")
	print("Ok")
except ed25519.BadSignatureError:
	print("Bad sig")
except Exception as e:
        print("Error"+str(e))
