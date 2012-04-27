#!/usr/bin/env python

import Crypto

from OpenSSL import crypto, SSL
from socket import gethostname
from pprint import pprint
from time import gmtime, mktime
from os.path import exists, join

CERT_FILE = "motmot.crt"
KEY_FILE = "motmot.key"

# creates a self signed cert for the server if one doesn't exist
def create_self_signed_cert(cert_dir, domain):
    """
    If datacard.crt and datacard.key don't exist in cert_dir, create a new
    self-signed cert and keypair and write them into that directory.
    """

    if not exists(join(cert_dir, CERT_FILE)) \
            or not exists(join(cert_dir, KEY_FILE)):
            
        # create a key pair
        k = crypto.PKey()
        k.generate_key(crypto.TYPE_RSA, 1024)

        # create a self-signed cert
        cert = crypto.X509()
        cert.get_subject().C = "US"
        cert.get_subject().ST = "Massachusetts"
        cert.get_subject().L = "Cambridge"
        cert.get_subject().O = "motmot"
        cert.get_subject().OU = "server"
        cert.get_subject().CN = domain
        cert.set_serial_number(1000)
        cert.gmtime_adj_notBefore(0)
        cert.gmtime_adj_notAfter(10*365*24*60*60)
        cert.set_issuer(cert.get_subject())
        cert.set_pubkey(k)
        cert.sign(k, 'sha1')

        open(join(cert_dir, CERT_FILE), "wt").write(
            crypto.dump_certificate(crypto.FILETYPE_PEM, cert))
        open(join(cert_dir, KEY_FILE), "wt").write(
            crypto.dump_privatekey(crypto.FILETYPE_PEM, k))

# this function will use the local cert to sign a client cert
def signCert(cert_dir, certStr):
    
    pk = crypto.PKey()
    # load private key
    f = open(join(cert_dir, KEY_FILE), "r")
    pk = crypto.load_privatekey(crypto.FILETYPE_PEM, f.read())
    
    # load certificate into X509 instance
    cert = crypto.load_certificate(crypto.FILETYPE_PEM, certStr)
    
    # sign it
    cert.sign(pk, 'sha1')

    # return string of cert
    return crypto.dump_certificate(crypto.FILETYPE_PEM, cert)
