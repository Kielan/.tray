#!/usr/bin/env node

var fsXtra = require('fs-extra');
var program = require('commander');
const resolveCwd = require('resolve-cwd');
const docPath = require('../config.js').docPath;
const returnFileAction = require('../lib/return');
const homedir = require('os').homedir();
const strTestD = String.raw`\n`

const returnTrueIfHasBacklash = (str) => {
    return str.endsWith(strTestD);
}

const lastIndexOfDirFileName = (str) => {
  var n = str.lastIndexOf('/');
  var result = str.substring(n + 1);
  return result;
}

program.version('0.0.1', '-v, --version')
//.description('cli tool for moving files quickly for env')

program.command('pack')
.arguments('<cmd> [env]')
.action(function (cmd, env) {
   let cmdFileNameValue = cmd;
   let cmdFileNameDestValue = lastIndexOfDirFileName(cmdFileNameValue);

   fsXtra.move(resolveCwd(`./${cmdFileNameValue}`), `${docPath}/${cmdFileNameDestValue}`, function (err) {
     if (err) return console.error(err)
     console.log("success!")
   })
});

program.command('return')
//.description('return file from tray to current working dir')
.arguments('<fileName>')
.action(async (fileName, env) => {
  console.log('tray.js return: ');
  try {
    await returnFileAction(fileName);
  } catch (error) {
    console.log('program action err: ', error);
  }
});

program.parse(process.argv);
