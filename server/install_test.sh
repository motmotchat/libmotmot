#!/bin/sh

# insert test users for testing

sqlite3 config.db "INSERT INTO users(userName, password) values ('ej@bensing.com','12345');"

sqlite3 config.db "INSERT INTO users(userName, password) values ('ej@bensing2.com','12345');"
sqlite3 config.db "INSERT INTO users(userName, password) values ('ej@bensing1.com','12345');"
