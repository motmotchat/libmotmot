#!/usr/bin/env python

import sqlite3 as lite

# this is essentially for running queries that either only have 1 return value or no return value
def execute_query(q, params):
    con = None

    try:
        con = lite.connect('config.db')
        cur = con.cursor()

        cur.execute(q, params)
        rVal = cur.fetchone()
        con.commit()
        return rVal
    except lite.Error, e:
        print "Error %s: " % e.args[0]
    finally:
        if con:
            con.close()


