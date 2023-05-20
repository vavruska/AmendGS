#### AmendGS - A small revision control system for the Apple IIgs
By Chris Vavruska

![alt text](https://github.com/vavruska/AmendGS/blob/main/assets/amendgs.png?raw=true)

AmendGS is a self-contained revision control system. It was originally going to be a port of Amend by Joshua Stein but I decided to add some additional functionality . For more information about Amend please visit the Amend web site at https://jcs.org/amend.

Just like the original, AmendGS is open source. The source can be found at https://github.com/vavruska/AmendGS. 

**Repo Browser**
Each project handled by AmendGS is contained in a single "repo" database file. A repo file must reside in the same directory as the files it is managing. When starting Amend without having double-clicked a repo from the Finder, one must use the File -> Open Repo to open a repo file. To create a new repo choose File -> New Repo.

Once a repo has been opened, the main browser interface of Amend is displayed:

![alt text](https://github.com/vavruska/AmendGS/blob/main/assets/repo.png?raw=true)

On the left are the list of files; each file being managed must be added through the Repo -> Add File menu option. In this dialog, files outside of the repository's directory and files that already exist in the repository are hidden.

On the top right are a list of commits. As files are selected on the left, the list of commits is narrowed down to only those that included the selected files.

Once a commit has been selected, the author, date, commit message, and diff text are shown in the bottom section.

**Committing**
After selecting one or more files, or leaving the default "[All Files]" selected, clicking Generate Diff will bring up the diff/commit window. As each selected file is compared to the previous version stored in the repo and found to be differing, a unified context diff is appended to the text box on the bottom.

Once diffing is complete, a log message must be entered and then the Commit button can be clicked. Each differing file's contents are updated in the repo and the commit timestamp, author, date, and diff text are stored. The diff/commit window is closed and the list of commits in the browser is updated.

**Settings**
The author/username used for new commits can be adjusted in the Settings menu. This global option is stored in the program's resource file, and are not unique per-repo since repos can be shared.

**What is different in AmendGS?**

While all the original source control is almost identical to the original there are a few key differences. They are as follows

**1. Patching is enabled**
Patching should now work. I would take care as it has not been extensively tested.

**2. Visualizations**
Visualizations is a way of displaying the differences between two versions side by side instead of just viewing a diff file. The visualize code uses the fonts from Prizm (ORCA editor).

![alt text](https://github.com/vavruska/AmendGS/blob/main/assets/commit.png?raw=true)

**3. Support for the Undo Manager**
All TextEdit controls support undo/redo if the Undo Manager is detected. See https://speccie.uk/software/undo-manager/ for more information on the Undo Manager

**4. Support for NiftySpell**
You can use NiftySpell to spell check any TextEdit control if NiftySpell is detected during initializatio.  For more information about NiftySpell see https://speccie.uk/software/niftyspell/

**Building AmendGS**

AmendGS was ported from the original source and developed using GoldenGate from Kelvin Sherlock. It can be compiled using the included makefile. ORCA/C 2.2.0 B7 is recommended. If you are really interested in building it using the ORCA Shell then let me know and I will create a build file for it. For more information on GoldenGate see https://juiced.gs/store/golden-gate/

**Thanks!**

Thanks to Joshua Stein for creating Amend and releasing as Open Source.  
Thanks to The ByteWorks, Stephen Heumann and Kelvin Sherlock for ORCA/C 2.2.0 B7
Thanks to GSPlus Magazine for the code (MiscLib) to create the about box.

End.
