# HTTP Request

command: http://nplinux7.cs.nctu.edu.tw:7777/test.html

response:
```
GET /test.html HTTP/1.1
Host: nplinux7.cs.nctu.edu.tw:7777
Connection: keep-alive
Cache-Control: max-age=0
Upgrade-Insecure-Requests: 1
User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/95.0.4638.69 Safari/537.36 Edg/95.0.1020.53
Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.9
Accept-Encoding: gzip, deflate
Accept-Language: en-US,en;q=0.9
Cookie: _ga=GA1.3.2116053485.1624962559
```

# Test
1. http://localhost:7777/printenv.cgi
2. http://localhost:7777/panel.cgi
3. http://localhost:7777/sample_console.cgi

```
GET /panel.cgi HTTP/1.1
```

# Ref
https://www.boost.org/doc/libs/develop/libs/beast/example/http/client/async/http_client_async.cpp