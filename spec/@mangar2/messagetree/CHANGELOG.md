# change log for messagetree

## 1.5.0 2022-01-09 update

- Added the ability to clean up the tree by deleting outdated nodes

## 1.4.0 2020-12-02 update

- Added the ability to split a timeValue history entry in a time entry and a timeValue entry
- Added the ability to split a time history entry in a time entry and an interval entry

## 1.3.0 2020-11-28 update

- removed the topics information from the getSection result. It is not needed.
- changed output format from Object of Objects to array of Objects to reduce the amount of data sent.
- added getNodes to retrieve a list of changed nodes

## 1.2.1 2020-11-27 patch

- removed test result files from package

## 1.2.0 2020-11-27 update

- Added test cases for getSection
- Separated test cases in directories
- Added flag to exclude the reason information of messages in getSection

## 1.1.0 2020-04-1 update

- Complete rewrite of tests, now near 100%
- Improved the jsdoc comments
- Added the ability to restrict the amount of values in timeValue and time nodes

## 1.0 Initial release

- Initial release
