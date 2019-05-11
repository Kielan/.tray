
//#!/usr/bin/env node

var fsXtra = require('fs-extra');
var program = require('commander');
var { docPath } = require('config.js');
const resolveCwd = require('resolve-cwd');
const homedir = require('os').homedir();

const path = ``;

const returnTrueIfHasBacklash = (str) => {
    return str.endsWith('/');
}

program
.version('0.0.1', '-v, --version')
.description('cli tool for moving files quickly for env')
.arguments('<cmd> [env]')
.action(function (cmd, env) {
   let cmdFileNameValue = cmd;
   let envValue = env;
   fsXtra.move(resolveCwd(`./${cmdFileNameValue}`), `${docPath}/${cmdFileNameValue}`, function (err) {
     if (err) return console.error(err)
     console.log("success!")
   })
});

// must be before .parse() since
// node's emit() is immediate
/*
program.on('--help', function(){
  console.log('')
  console.log('Examples:');
  console.log('  $ custom-help --help');
  console.log('  $ custom-help -h');
});
*/

//program.command('list', 'list files in tray').action(function() {})

program.command('clear', 'clear stored files').action(function() {

});

program.command('return [fileName]')
.description('return file from tray to current working dir')
.action(function(cmd, env) {
  let cmdFileNameValue = cmd;
  let envValue = env;

  console.log('program.command return backslash? mockTestFile.js\: ', cmdFileNameValue);

  fsXtra.move(`${homedir}/Documents/Programming/.tray/${cmdFileNameValue}`, resolveCwd(`./${cmdFileNameValue}`), function (err) {
    if (err) return console.error(err)
    console.log("success!")
  })
});

program.parse(process.argv);
