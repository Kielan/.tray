var path = require('path');
var fsXtra = require('fs-extra');
const resolveCwd = require('resolve-cwd');
const homedir = require('os').homedir();

module.exports = async (fileName, done) => {
  await fsXtra.move(`${homedir}/Documents/Programming/.tray/${fileName}`, path.resolve(`./__mocks__/${fileName}`), function(err) {//resolveCwd(`./`), function (err) {
    if (err) return console.log(err)
    console.log("success!");
  });
  done();
}
