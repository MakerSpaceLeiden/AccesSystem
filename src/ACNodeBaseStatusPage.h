#if 0
static const char *htmlStatusPageContent PROGMEM = R"(
<!DOCTYPE html>
<html>
<head><title>Status report</title><head>
<script>
function render(o) {
 if (o === null) 
   return "<font color=red>NULL</color>";
 else if (typeof o === "string")
   return "<font color=black>"+ o + "</color>";
 else if (typeof o === "number")
   return "<font color=darkblue>"+ o + "</color>";
 else if (typeof o === "boolean")
   return o ? "TRUE" : "FALSE";
 else if (Array.isArray(o)) {
   var str= "";
   for(const e of o) 
      str += render(e) + "</br>";
   return str;
 }
 else if (o.constructor == Object) {
   var str= "";
   for( const k of Object.keys(o).sort()) 
          str +=  "<tr valign=top><td align=right><code>"+ k + "</code></td><td bgcolor=lightblue>" + render(o[k])+"</td></tr>";
   return "<table>" + str + "</table>";      
 }
 return o;    
};

function onLoad() {
  const url = 'state.json';
  fetch(url)
    .then(res => res.json())
    .then(out => { document.getElementById("report").innerHTML = "<h1><hr>Node " + out['node']+"<hr></h1>" + render(out); })
    .catch(err => console.log(err));
}
</script>
<body onload='onLoad()'><div id='report'></div></body>
</html>
)";
#else
static const char *jsRenderJsonContent PROGMEM = R"(
function rr(r){if(null===r)return"<font color=red>NULL</color>";if("string"==typeof r)return"<font color=black>"+r+"</color>";if("number"==typeof r)return"<font color=darkblue>"+r+"</color>";if("boolean"==typeof r)return r?"TRUE":"FALSE";if(Array.isArray(r)){var o="";for(const t of r)o+=rr(t)+"</br>";return o}if(r.constructor==Object){o="";for(const t of Object.keys(r).sort())o+="<tr valign=top><td align=right><code>"+t+"</code></td><td bgcolor=lightblue>"+rr(r[t])+"</td></tr>";return"<table>"+o+"</table>"}return r}function o(u){fetch(u).then((r=>r.json())).then((r=>{document.getElementById("jsRender").innerHTML="<h1><hr>Node "+r.node+"<hr></h1>"+rr(r)})).catch((r=>console.log(r)))}
)";
static const size_t jsRenderJsonContentLength = strlen_P(jsRenderJsonContent);

static const char *htmlStatePageContent PROGMEM = R"(
<!DOCTYPE html><html><head><title>State</title><head><script src="jsRenderJson.js"></script></head><body onload='o("state.json")'><div id='jsRender'></div></body></html>
)";
static const size_t htmlStatePageContentLength = strlen_P(htmlStatePageContent);

static const char *htmlReportPageContent PROGMEM = R"(
<!DOCTYPE html><html><head><title>Report</title><head><script src="jsRenderJson.js"></script></head><body onload='o("report.json")'><div id='jsRender'></div></body></html>
)";
static const size_t htmlReportPageContentLength= strlen_P(htmlReportPageContent);

#endif

