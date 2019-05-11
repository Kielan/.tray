/*
search for .tray inside of a list of files
*/
var fs = require('fs');
var path = require('path');
var docPath = require('./config.js').docPath;

module.exports.searchForTray = () => {
  var exists = fs.existsSync(`${docPath}`);

  return exists;
}

module.exports.testTrayAsDir = () => {
  var stats = fs.statSync(`${docPath}`);

  return stats.isDirectory();
}

module.exports.testFileExists = async (path) => {
  try {
    if (fs.existsSync(path)) {
      //file exists
      return true;
    }
  } catch(err) {
    console.error(err)
    return false;
  }
}

const deleteFolderRecursive = async (directory_path) => {
    if (fs.existsSync(directory_path)) {
        fs.readdirSync(directory_path).forEach(function (file, index) {
            var currentPath = path.join(directory_path, file);
            if (fs.lstatSync(currentPath).isDirectory()) {
                deleteFolderRecursive(currentPath);
            } else {
                fs.unlinkSync(currentPath); // delete file
            }
        });
    }
};

module.exports.deleteFolderRecursive = deleteFolderRecursive;
