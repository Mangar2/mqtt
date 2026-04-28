# change log for rules

## 3.0.0 2023-04-10 release

- Changed the internal rule structure from array to object indexed with the rule name
- Add methodes to set, update and delete single rules
- Adds Weekdays option to select days of the week for a rule
- Reason extended now including value evaluation
- Adds error property to rules, which contains the current error message for rule processing
- Error message also shows undefined variable messages

## 2.2.2 2023-01-14 minor update

- Added optional boolean "active" flat to rules to disable rules

## 2.2.1 2022-11-13 update

- Improved logging

## 2.2.0 2022-09-30 update

- messages having time values are now treated as duplicates and will not been send in cooldown phase, even if they have different times

## 2.1.1 2022-06-29 fix

- correctly dertimine used variables

## 2.1.1 2022-06-14 fix

- added the doLog property in the rule-schema
- improved the logging

## 2.1 2022-06-14 update

- added ability to log a rule evaluation - if it has the "doLog" attribute true

## 2.0 2020-27-10 update

- Refactoring, added a "variables" class storing required variables
- Removed "internal" variables (e.g. sunset/sunrise) from the list of used variables

## 1.6.0 2020-27-10 update

- Added the delayInSeconds property for rules

## 1.5.0 2020-27-10 update

- Added non motion events to be checked by anyOf, allOf, nonOf

## 1.4.3 2020-05-04 fix

- Fixed typo in package.json file
