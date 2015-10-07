Starting `pianod`
-----------------
### Configuration
`pianod` reads its configuration file from `~/.config/pianod/startscript` (when running as root, `/etc/pianod.startscript` is used instead).  `startscript` runs as the `pianod` administrator, uses the same syntax and commands as the socket interface.  A sample startscript is provided in the `contrib` directory.

`pianod` also maintains a user list in `~/.config/pianod/passwd` (root: `/etc/pianod.passwd`).  If the file does not exist, a single user `admin` (password `admin`) is created.  This user is persisted, so if you create your own administrator account be sure to delete `admin`.

### Launching at boot or login
Unlike older UNIX daemons, `pianod` does not use `fork`(2)/`exec`(2) or `daemon`(3) on startup.  Supplied sample configuration files expect `pianod` to be installed in `/usr/local/bin`; if this is not right you will need to revise the files when installing.

If `pianod` is started as root, it will use `/etc/pianod.startscript` and `/etc/pianod.passwd` for configuration files.  After establishing the listener socket, `pianod` drops root privileges and takes on a user persona, by default the user 'nobody'.  It also takes the primary group of this user.  To allow users to be persisted, the password file is reassigned to this user.  The user can be selected via `-n`.

In r149 and earlier, `pianod` kept root's supplementary groups.  In r150 and later, the `-g` option can be used to specify a comma-separated list of groups.  If `-g` is omitted, supplementary groups are taken for nobody (or the user specified with `-n`).

*`pianod` must not be installed setuid.*  Using a combination of -i and -n options, a malicious user could arbitrarily reassign file ownership.

On OS X:

* Included in contrib is an example com.deviousfish.pianod.plist.
* When installing the file, be sure to review the file as there are some file paths and user names in it.
* To launch `pianod` upon login of a particular user, install the file in that user's Library/LaunchAgents directory.
* To launch `pianod` at boot, place it in the system's /Library/LaunchDaemons directory.  Edit the file to enable the UserName key, setting it to run as your desired user.

On Linux with `systemd`(8):

* Included in contrib is pianod.service for systems using systemd(1).
* To launch `pianod` at boot, place it in `/usr/local/lib/systemd/system` and put the startscript in `/etc/pianod.startscript`.
* To launch `pianod` at login, install the file in the user's `.config/systemd` directory [Need verification].  You will want to remove `User=nobody` and `-i /etc/pianod.startscript` from the `[Service]` section of the file.
* After installing the file run "systemctl start pianod" to start the daemon and "systemctl enable pianod" to enable it to start on boot.
* The sample expects pianod to be installed in /usr/sbin; set your configure options or modify the start file as needed.

On systems using /etc/init.d scripts:

* Included in contrib is pianod.raspian.init.
* The script is for the Raspbian distribution but can be modified as needed.
* The sample expects pianod to be installed in /usr/sbin; set your configure options or modify the start file as needed.

For systems that use init without /etc/init.d, you'll need to edit the inittab file.  Add this to `/etc/inittab`:

	pd:23:respawn:su - username -c /usr/local/bin/pianod

On Windows, you are on your own.

### Start Options
See the [pianod(1)] manual page for the full list of command line options.

[pianod(1)]: http://deviousfish.com/pianod/pianod.txt
