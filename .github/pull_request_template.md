<!-- 
This document serves as a simple example of a PR template.
It also contains some recommended headings along with some
other useful headers. At the end of the document are some
extra blocks of Markdown that could be used in many 
different PRs and the hope is they can be copied and pasted.
-->

<!-- ( Optional Header ) --\>
## Pre-PR Checklist 

- [ ] New code sufficiently commented and documented?
- [ ] New code passes unit tests?

***
-->

## Purpose of this PR
<!-- ( Recommened Header )
List here the problems that the PR is trying to fix.
Use your JIRA cards for inspiration for this section.

Examples:
-->
* To improve web lib interface
* To improve documentation
* To add access to more c functions needed in projects

***

## Changes made in this PR
<!-- ( Recommened Header )
Give a brief description of the work done on each issue
listed in the section above.

Consider using screenshots/videos to describe your work:
https://docs.github.com/en/get-started/writing-on-github/working-with-advanced-formatting/attaching-files

Example:
-->
* Improved interface to vol_web 
  * Added `pre.js`, which is added to the wasm module during compilation and provides the function that creates the new interface 
* Added access to c functions that return the index of the current loaded frame, and total amount of frames in a vologram
* Added documentation for three.js
* Added info to readme


***

## Testing Summary
<!-- ( Recommened Header )
Explained how reviewers build, deploy, and test your code and what should they look for that would indicate a bug or regression.

This is an opportunity to add the base build and testing commands to the template that developers can copy, paste and/or modify to hightlight any changes to the build process (e.g. as a result of a new dependency):
-->

### Build Command

* Created 2 build scripts for building js code and js module
* Added `--pre.js` flag to add function that creates nicer interface to the wasm module

```sh
cd wasm
./build-mjs.sh
```
<!--
State what devices, iOS'es, browsers, etc. were tested on. You can also ask
for others to help test on platforms you cannot.

Example:
-->

### Checklist

#### Chrome on Mac 

##### Import Type

- [ ] Direct file import 
- [ ] npm
- [ ] cdn

##### Build type

- [ ] Module built with `build-mjs.sh`
- [ ] Code built with `build-js.sh`

***

## Risk Introduced
<!-- ( Recommended Header ) -->

- [ ] Gaps in testing - need to test npm and cdn before merge
- [ ] Change to build scripts could fail on untested platforms

***

# Pre-Merge Checklist

<!-- ( Recommended Header ) 
If you have a problem with the review process (e.g. it's taking too long, or the reviewers forgot) get in touch with the Team Lead of the project!
-->

- [ ] No conflicts with destination branch
- [ ] Reviewed your own PR and checked your changed code
- [ ] Invited reviewers (ideally +2)
- [ ] All questions/comments answered (where necessary)
- [ ] Approvals received after latest changes (ideally 2+)
<!-- - [ ] CI/CD builds successful -->

***