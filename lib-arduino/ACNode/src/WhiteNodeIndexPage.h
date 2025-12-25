static const char *htmlIndexPageContent PROGMEM = R"(
<!DOCTYPE html>
<html>
<head><title>node %NODE%</title><head>
<body>
The local time at this node is %TIME% 
<a href="/log/log.html">log</a>,
<a href="/state.html">state</a> 
<a href="/display.html">display</a> 
</body>
</html>
)";


static const size_t htmlIndexPageContentLength= strlen_P(htmlIndexPageContent);
