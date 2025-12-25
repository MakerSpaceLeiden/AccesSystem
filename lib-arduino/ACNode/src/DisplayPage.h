static const char *htmlDisplayPageContent PROGMEM = R"(
<!DOCTYPE html>
<html>
<body>
<canvas id="display"></canvas>
<script>
const c=document.getElementById("display");function render(t,a){if(80!=t[0]||52!=t[1]||10!=t[2])return void console.log("Not a P4 image. Ignoring");for(var e="",o=3;10!=t[o];)e+=String.fromCharCode(t[o++]);const[n,r]=e.split(" ");o++;const d=a.getContext("2d"),c=d.createImageData(n,r);for(var i=0,g=0;g<r;g++)for(var s=0;s<n;s++){const a=s+g*n,e=t[o+(a>>3)]&1<<7-(7&a)?0:255;c.data[i++]=e,c.data[i++]=e,c.data[i++]=e,c.data[i++]=255}d.putImageData(c,(a.width-n)/2,(a.height-r)/2)}fetch("display.pbm").then((t=>t.bytes())).then((t=>render(t,c)));
</script>
)";


static const size_t htmlDisplayPageContentLength= strlen_P(htmlDisplayPageContent);
