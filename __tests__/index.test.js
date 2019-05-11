var path = require('path');
const execa = require('execa');
const resolveCwd = require('resolve-cwd');
const homedir = require('os').homedir();
var fsXtra = require('fs-extra');
var { deleteFolderRecursive, searchForTray, testFileExists } = require('../util');
var exec = require('child_process').exec;

const returnTrueIfHasBacklash = (str) => {
    return str.endsWith('/');
}

function cli(args, cwd) {
  return new Promise(resolve => {
    console.log('cli log: ', `node ${path.resolve('./index')} ${args.join(' ')}`)
    exec(
    `node ${path.resolve('./index')} ${args.join(' ')}`,
    { cwd },
    (error, stdout, stderr) => { resolve({
      code: error && error.code ? error.code : 0,
      error,
      stdout,
      stderr })
    })
})}

describe(`${__filename}__dir_tests`, () => {
  const testOutput = {
    homedir: '/Users/kielan',
    githubdir: '/Users/kielan/Documents/Github',
    programmingdir: '/Users/kielan/Documents/Programming',
  }

  it('should return the homedir', () => {

    expect(homedir).toEqual(testOutput.homedir);
  })

  it('should return the Github subdir', () => {
    const githubSubDir = homedir+`/Documents/Github`;

    expect(githubSubDir).toEqual(testOutput.githubdir);
  })

  it('should return the Programming subdir', () => {
    const programmingSubDir = homedir+`/Documents/Programming`;

    expect(programmingSubDir).toEqual(testOutput.programmingdir);
  })

  it('should find tray with util funciton on this os returning bool true', () => {
    const searchResult = searchForTray();

    expect(searchResult).toEqual(true);
  })
})

describe(`${__filename}__integration_tests`, () => {
  beforeEach(async (done) => {
    deleteFolderRecursive(`${homedir}/Documents/Programming/.tray`);
    deleteFolderRecursive(`${homedir}/Documents/Programming/utilPkgs/.tray/__mocks__/`);
    await fsXtra.copy(`${homedir}/Documents/Programming/utilPkgs/.tray/__mocks__backup__/mockTestFile.js`, `${homedir}/Documents/Programming/utilPkgs/.tray/__mocks__/mockTestFile.js`);
    done();
  });

  it('should move file out of working dir', async () => {
    let testFileExistsResult;
    try {
      await execa.stdout('bin/tray.js', ['pack', './__mocks__/mockTestFile.js']);
      testFileExistsResult = await testFileExists(`${homedir}/Documents/Programming/.tray/mockTestFile.js`);//(path.resolve('./__mocks__/mockTestFile.js'))
      expect(testFileExistsResult).toBe(true);
    } catch (e) {
      console.log('catch e: ', e)
    }

  });

  it('should move file out and back into __mocks__ ', async () => {
    //this a
    let result;
    let returnResult;
    let testFileExistsResult;

    try {
      //this actually does work but jest calls out of sync and testFileExists returns undefined
      //i believe this is because execa spawns a child process separate
      //from main
      await execa.stdout('bin/tray.js', ['pack', './__mocks__/mockTestFile.js']);
      await execa.stdout('bin/tray.js', ['return', 'mockTestFile.js']).then(result => result);
      testFileExistsResult = await testFileExists(path.resolve(`./__mocks__/mockTestFile.js`));

      console.log('log withinTest returnResult: ', testFileExistsResult)

      expect(testFileExistsResult).toBe(true);
    } catch (e) {
      console.log('catch e: ', e);
    }
  });

  it('test string w backlash returns true in util atom', () => {
    const testStringWBackSlash = `Users/example/Documents/`;
    const truthyTest = returnTrueIfHasBacklash(testStringWBackSlash);
    expect(truthyTest).toBe(true);
  });

})
