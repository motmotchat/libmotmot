#!/bin/bash

if [ -z $1 ]; then
  echo 'Usage: ./mkcrt.sh COMMON-NAME'
  exit
fi

OUT='mycert'

while getopts 'o:' OPTION
do
  case $OPTION in
    o) OUT=$OPTARG ;;
  esac
done

openssl req \
  -x509 -nodes -days 365 \
  -subj "/C=US/ST=Massachusetts/L=Cambridge/CN=$1" \
  -newkey rsa:4096 -keyout $OUT.key -out $OUT.crt
