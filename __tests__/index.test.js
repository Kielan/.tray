describe(`${__filename}__dir_tests`, () => {
  const testOutput = {
    homedir: '/Users/kielan',
    githubdir: '/Users/kielan/Documents/Github',
    programmingdir: '/Users/kielan/Documents/Programming',
  }
  it('should return the homedir', () => {
    const homedir = require('os').homedir();

    expect(homedir).toEqual(testOutput.homedir)
  })

  it('should return the Github subdir', () => {
    const homedir = require('os').homedir();
    const githubSubDir = homedir+`/Documents/Github`

    expect(githubSubDir).toEqual(testOutput.githubdir)
  })

  it('should return the Programming subdir', () => {
    const homedir = require('os').homedir();
    const programmingSubDir = homedir+`/Documents/Programming`

    expect(programmingSubDir).toEqual(testOutput.programmingdir)
  })
})
