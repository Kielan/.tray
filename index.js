
#!/usr/bin/env node

var fs = require('fs-extra');
var program = require('commander');
const resolveCwd = require('resolve-cwd');
const homedir = require('os').homedir();

const path = ``;

program
.version('0.0.1')
.description('cli tool for moving files quickly for env')
.arguments('<cmd> [env]')
.action(function (cmd, env) {
   cmdFileNameValue = cmd;
   envValue = env;
   fs.move(resolveCwd(`./${cmdFileNameValue}`), `/tmp/does/not/exist/yet/somefile`, function (err) {
     if (err) return console.error(err)
     console.log("success!")
   })
});

//program.command('list', 'list files in tray').action(function() {})

program.parse(process.argv);
