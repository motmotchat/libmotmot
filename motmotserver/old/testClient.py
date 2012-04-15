#!/usr/bin/env python
# coding: utf-8


import msgpackrpc

if __name__ == '__main__':
    client = msgpackrpc.Client(msgpackrpc.Address("localhost", 8888))
    """
    method = raw_input("Enter Method Name: ")

    args = []

    arg = raw_input("Enter Arguement(to finish, submit blank line):")
    while(arg != ""):
        args.append(arg)
        arg = raw_input("Enter Arguement(to finish, submit blank line):")

    print args

    def makeCall(cl, methodName, *args):
        r1 = cl.call(methodName, *args)
        return r1

    res = makeCall(client, method, args)
    
    print res

    """
    
    client.call("authenticate","ebensing","12345")
    res = client.call("echo", "123456")

    print res
