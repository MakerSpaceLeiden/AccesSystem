static const char *htmlDisplayPageContent PROGMEM = R"(
<!DOCTYPE html><html><body bgcolor=black><canvas id="display" style="image-rendering: pixelated; width: 100%;"></canvas><script>const c=document.getElementById("display");function rr(t,a){if(80!=t[0]||52!=t[1]||10!=t[2])return void console.log("Not a P4 image. Ignoring");for(var e="",o=3;10!=t[o];)e+=String.fromCharCode(t[o++]);const[n,r]=e.split(" ");o++;const d=a.getContext("2d"),c=d.createImageData(n,r);for(var i=0,g=0;g<r;g++)for(var s=0;s<n;s++){const a=s+g*n,e=t[o+(a>>3)]&1<<7-(7&a)?0:255;c.data[i++]=e,c.data[i++]=e,c.data[i++]=e,c.data[i++]=255}d.putImageData(c,(a.width-n)/2,(a.height-r)/2)}fetch("display.pbm").then((t=>t.bytes())).then((t=>rr(t,c)));
</script></html>
)";

static const size_t htmlDisplayPageContentLength= strlen_P(htmlDisplayPageContent);

#if 0
const c = document.getElementById("display");

fetch('http://localhost/x/display.pbm')
	.then(resp => resp.bytes())
	.then(resp => render(resp, c));

function render(data, c) {
        if (data[0] != 80 || data[1] != 52 || data[2] != 10) {
		console.log("Not a P4 image. Ignoring");
		return;
	};
        var dims = "", i = 3;
        while(data[i] != 10) 
		dims += String.fromCharCode(data[i++]);
        const [w,h] = dims.split(" ");
	i++; // skip trailing \n

        c.width = w;
        c.height = h;

	const ctx = c.getContext("2d");
	const raw = ctx.createImageData(w, h);

	var j = 0;
        for(var y = 0; y < h; y++) 
           for(var x = 0; x < w; x++) {
              const o = x + y * w;
              const bv = data[ i +  (o >> 3)] & (1<< (7-(o & 7))) ? 0 : 255;
	      raw.data[j++] = bv;
	      raw.data[j++] = bv;
	      raw.data[j++] = bv;
	      raw.data[j++] = 255;
	}
	ctx.putImageData(raw, (c.width - w)/2, (c.height-h)/2);
}
#endif

