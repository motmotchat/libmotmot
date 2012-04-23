#!/bin/sh

#This script bootstraps a new sqlite db for testing
sqlite3 config.db "CREATE TABLE users(userId INTEGER PRIMARY KEY AUTOINCREMENT, userName TEXT, password TEXT);"
sqlite3 config.db "INSERT INTO users(userName,password) values('ebensing@bensing.com','12345');"
sqlite3 config.db "CREATE TABLE friends(friendId INTEGER PRIMARY KEY AUTOINCREMENT, userName TEXT, friend TEXT, accepted BOOLEAN);"

