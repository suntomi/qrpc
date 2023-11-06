
r1=$(curl -sS http://localhost:8888/accept)
if [ "${r1}" != '{"sdp":"hoge"}' ]; then
  echo "1: Unexpected response: ${r1}"
  exit 1
fi
# set field separator to newline
IFS='
'
r2=($(curl -sS -w "%{http_code}" http://localhost:8888/accep))
if [ "${r2[0]}" != 'no route matched for /accep' ]; then
  echo "2[0]: Unexpected response: ${r2[0]}"
  exit 1
fi
if [ "${r2[1]}" != '404' ]; then
  echo "2[1]: Unexpected response: ${r2[1]}"
  exit 1
fi
unset IFS

