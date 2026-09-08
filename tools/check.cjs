const fs=require('node:fs'),vm=require('node:vm');
const html=fs.readFileSync(require('node:path').join(__dirname,'../src/index.html'),'utf8');
new vm.Script(html.split('<script>')[1].split('</script>')[0]);
console.log('Embedded browser JavaScript syntax: PASS');
