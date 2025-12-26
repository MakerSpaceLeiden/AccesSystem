static const char *htmlIndexPageContent PROGMEM = R"(
<!DOCTYPE html>
<html>
<head><title>node %NODE%</title><head>
<body>
The local time at node %NODE% is %TIME%.
<p>
<hr><i>
<a href="/log">log</a>,
<a href="/state">state</a> 
<a href="/display">display</a> 
</body>
</html>
)";


static const size_t htmlIndexPageContentLength= strlen_P(htmlIndexPageContent);
