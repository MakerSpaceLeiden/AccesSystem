static const char *htmlIndexPageContent PROGMEM = R"(
<!DOCTYPE html>
<html>
<head><title>node %NODE%</title><head>
<body>
The local time at this node is %TIME% 
<a href="/log">log</a>,
<a href="/state">state</a> 
<a href="/display">display</a> 
</body>
</html>
)";


static const size_t htmlIndexPageContentLength= strlen_P(htmlIndexPageContent);
