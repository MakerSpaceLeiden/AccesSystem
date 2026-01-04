#include <ESPAsyncWebServer.h>
static const char webPage[] PROGMEM = R"(
<html>
  <head>
      <title>Kiosk Login</title>
  </head> 
  <style> 
      * { font-family: Gill-Sans, GillSans, Helvetica, Arial, sans-serif; }
  </style>
  <body>
   <center>
      <h2>Olga KIOSK login</h2>
<svg width='100' height='100' clip-path='inset(0% round 15px)' viewBox='0 0 38.100037 38.100006'>
  <g transform='translate(-12.966463,-12.267849)'>
    <path style='fill:#ff0000;stroke-width:2.82223;paint-order:fill markers stroke' d='M 12.966463,12.26785 H 51.066496 V 50.36786 H 12.966463 Z' />
    <path style='fill:#ffffff;stroke-width:0.352778;paint-order:fill markers stroke' d='m 23.70537,17.21737 c -0.535764,0.007 -1.070892,0.0881 -1.586477,0.24908 1.58616,1.58654 2.108977,2.05843 3.695383,3.64474 -0.282187,1.05282 -0.563316,2.10579 -0.845961,3.15847 -1.052654,0.28254 -2.105837,0.56431 -3.158455,0.84697 -1.586441,-1.5865 -2.109822,-2.05823 -3.696405,-3.64473 -0.684565,2.17927 0.07384,4.69662 1.81229,6.16706 1.062602,0.92903 2.468103,1.46016 3.876251,1.45417 0.652639,0.009 1.297341,-0.10456 1.907364,-0.31729 l 2.0955,2.09548 3.741878,-3.74189 -2.11148,-2.1115 C 30.137956,22.9643 29.582331,20.55061 28.043091,19.01572 26.92669,17.84703 25.312626,17.19738 23.70537,17.21737 Z m 12.496906,14.56759 -3.741385,3.74137 2.176604,2.17662 c -0.20634,0.60496 -0.313619,1.23926 -0.317782,1.87843 -2.47e-4,3.27975 2.65818,5.93865 5.937602,5.93866 3.279422,10e-6 5.937885,-2.65889 5.937638,-5.93866 -3.5e-5,-3.27956 -2.658392,-5.93814 -5.937638,-5.93814 -0.639339,0.004 -1.273845,0.1114 -1.878965,0.31781 z m 4.919592,4.65708 2.31147,2.31149 -0.845925,3.15794 -3.157961,0.84595 -2.311471,-2.31149 0.845926,-3.15794 z' />
    <path style='display:inline;fill:#ffffff;stroke-width:0.352778;paint-order:fill markers stroke' d='M 42.984288,18.24315 26.773373,34.44682 c -0.873972,-0.5166 -1.889795,-0.81494 -2.971377,-0.81494 -3.230703,0 -5.881298,2.65111 -5.881298,5.88181 0,3.23069 2.650595,5.88181 5.881298,5.88181 3.230704,0 5.882323,-2.65112 5.882323,-5.88181 0,-1.06882 -0.291465,-2.0734 -0.796855,-2.94039 l 7.404206,-7.4011 3.071142,3.07113 2.120265,-2.12028 -1.660878,-1.66037 2.048475,-2.04845 1.659819,1.65985 2.1203,-2.12029 -3.068566,-3.06854 2.522325,-2.5213 z M 23.801996,36.63014 c 1.610113,0 2.883535,1.27342 2.883535,2.88355 0,1.61013 -1.273422,2.88303 -2.883535,2.88303 -1.610148,0 -2.88304,-1.2729 -2.88304,-2.88303 0,-1.61013 1.272892,-2.88355 2.88304,-2.88355 z' />
  </g>
</svg>
      <p></p>
	<div id=info>...</div>
   </center>
   </body>
  <script language='javascript'>
     var aliveTimeout;
     var info;
     var gateway = 'ws://'+location.host+'/ws/tagreader';
     var websocket;
     window.addEventListener('load', onload);

     function resetKeepAlive() {
         if (aliveTimeout)
             clearTimeout(aliveTimeout);
         aliveTimeout = setTimeout(initWebSocket, 5000);
     };

     function onload(event) { 
        info = document.getElementById('info')
        info.innerHTML = 'searching for tag reader';
        initWebSocket(); 
     }

     function onOpen(event) { 
        info.innerHTML = 'swipe tag on reader';
     };

     function onClose(event) { 
        info.innerHTML = 'no connection with tag reader';
        setTimeout(initWebSocket, 2000); 
     }

     function onMessage(event) {
         resetKeepAlive();
         console.log(event);
         if (event.data)
            info.innerHTML = event.data;
         else 
           info.innerHTML = '<nope>';
     }
     function initWebSocket() {
         websocket = new WebSocket(gateway);
         websocket.onopen = onOpen;
         websocket.onclose = onClose;
         websocket.onmessage = onMessage;
         info.innerHTML = 'connecting';
         console.log('(re)Connecting)');
     }
</script>
</html>
)";
size_t webPageLength = sizeof(webPage);

AsyncWebSocket ws("/ws/tagreader"); // hardcoded -- Keep URL aligned with above javascript

