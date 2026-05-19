var XMLHttpRequest = require("xmlhttprequest").XMLHttpRequest;

const STROOM_TARIEF_COUNT = 80; // Max app. 3 days: before, day itself, day after.

function fetchStroom(includevat, includetax) {
  var today = new Date();
  var strday = String(today.getDate());
  if ( strday.length < 2 ) strday = '0' + strday;
  var strmonth = String(today.getMonth()+1);
  if ( strmonth.length < 2 ) strmonth = '0' + strmonth;
  var datestr = strday + '-' + strmonth + '-' + String(today.getFullYear());
  console.log('GET', 'https://public.api.energyzero.nl/public/v1/prices?interval=INTERVAL_HOUR&energyType=ENERGY_TYPE_ELECTRICITY&date=' + datestr);
  var req = new XMLHttpRequest();
  // curl -L 'https://public.api.energyzero.nl/public/v1/prices?date=24-03-2026&interval=INTERVAL_HOUR&energyType=ENERGY_TYPE_ELECTRICITY' -H 'Accept: application/json'
  // https://www.stroomperuur.nl/ajax/tarieven.php?datum=2025-12-27
  req.open('GET', 'https://public.api.energyzero.nl/public/v1/prices?interval=INTERVAL_HOUR&energyType=ENERGY_TYPE_ELECTRICITY&date=' + datestr, true);
  req.onload = function () {
    //console.log("OnLoad " + req.readyState);
    if (req.readyState === 4) {
      const buffer = new ArrayBuffer((STROOM_TARIEF_COUNT+1) * 4);
      const data = new Uint32Array(buffer);
      itemcount = 0;
      //console.log(req.status);
      if (req.status === 200) {
        try {
          //console.log(req.responseText);
          response = JSON.parse(req.responseText);
          todayhours = Math.floor(today.valueOf() / 3600000 / 24) * 24 + today.getTimezoneOffset() / 60;  // Returned values are in UTC.
          startdate = new Date(response.range.start).valueOf() / 1000;
          startdatehours = startdate / 3600;
          enddatehours = new Date(response.range.end).valueOf() / 3600000;
          itemcount = enddatehours - todayhours;
          console.log(today.valueOf(), new Date(response.range.end).valueOf());
          console.log(today, response.range.end);
          console.log(todayhours, enddatehours);
          console.log(itemcount);
          data[0] = today.valueOf() / 1000; // In seconds.
          if ( itemcount <= STROOM_TARIEF_COUNT ) {
            if ( includevat && includetax ) {
              values = response.all_in_with_vat;
            }
            else if ( includevat ) {
              values = response.base_with_vat;
            }
            else if ( includetax ) {
              values = response.all_in;
            }
            else {
              values = response.base;
            }
          }
          for ( const [key, item] of Object.entries(values) ) {
            startitemhours = new Date(item.start).valueOf() / 3600000;
            index = startitemhours - todayhours + 1;
            if ( index > 0 && index < STROOM_TARIEF_COUNT ) {
              data[index] = item.price.value * 100000;
            }
          }
          //console.log(Array.from(new Uint8Array(buffer, 0, (itemcount+1)*4)));
        } catch(error) {
          console.log("Error:");
          console.log(error);
        }
      }
      //console.log("Send response");
      //Pebble.sendAppMessage({Stroom: Array.from(new Uint8Array(buffer, 0, (itemcount+1)*4))});
    }
  };
  req.send(null);
}

fetchStroom(true, true);
