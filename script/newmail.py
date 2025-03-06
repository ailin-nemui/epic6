#
# A new mail script, to implement what we had in epic5.
#
# Here's the plan...
#  - I want mail support that isn't a regression from epic5.
#  - I don't need it to be 100% perfect
#  - It should not require any configuration by the user
#
#  Model - Convert a "mailbox" into a list of message-id's
#    1. Enumerate each email in the source - save the "message-id's"
#
#  View - Convert a "mailbox" into a new mail count
#    1. Diff the current email set from the prevoius set.
#    2. This tells us how many new emails, and a total count.
#    3. Save the current set for the next check
#
#  Controller - User-configurable knobs
#    1. How often will mail be checked (/timer)
#    2. How will new email be announced (/on mail)
#    3. How will the email count be persisted? (status bar)
#    4. How does the user indicate what to check?
#       4-1. Can all local mailboxes be autodetected?
#
# Both ways use the same consequence:
#     - ANNOUNCE IT
#      - Throw /on mail (or whatever), output "You have new mail"
#     - NO MAIL
#      - Set /set status_mail (or whatever) to the empty string
#     - STATUS BAR
#      - Set /set status_mail (or whatever) to (Mail: <count>)

import logging
import mailbox
import hashlib
import os
import epic
import traceback


#######
def mbox_emails(file):
    try:
        m = mailbox.mbox(file)
        r = [hashlib.sha256(d["message-id"].encode("ISO-8859-1")).hexdigest() for d in m]
    except Exception as e:
        logging.error("Exception: %s" % (e,))
        logging.error(traceback.format_exc())
    return r

def maildir_emails(path):
    try:
        m = mailbox.Maildir(path)
        r = [key for key,msg in m.iteritems() if "S" not in msg.get_flags()]
    except Exception as e:
        logging.error("Exception: %s" % (e,))
        logging.error(traceback.format_exc())
    return r

prev = []
@epic.alias('check_mail')
def check_mail(args):
    global prev

    try:
        if args is None: 
            return

        schema = args.split()[0]
        file = args.split()[1]

        if schema is None or file is None:
            return

        if schema == "mbox":
            curr = mbox_emails(file)
        elif schema == "maildir":
            curr = maildir_emails(file)
        else:
            curr = []             # I don't support this schema yet.
        curr_count = len(curr)

        if curr_count == 0:
            epic.eval("^set -status_mail")
        else:
            epic.eval("^set status_mail  (Mail: %d) " % (curr_count,))
            new_count = 0
            for c in curr: 
                if c not in prev:
                    new_count = new_count + 1

            prev = curr
            if (new_count > 0):
                epic.xecho('There are %d new email(s), %d total' % (new_count, curr_count))

    except Exception as e:
        logging.error("Exception: %s" % (e,))
        logging.error(traceback.format_exc())

########
@epic.alias('set_mail_on')
def set_mail_on(args):
    # Look for maildir...
    try:
        maildir = None
        if "MAILDIR" in os.environ and os.environ["MAILDIR"] and os.path.isdir(os.environ["MAILDIR"]):
           for sd in ["new", "cur"]:
               d = "%s/%s" % (os.environ["MAILDIR"], sd)
               if not os.path.isdir(d):
                   epic.xecho("The environment variable MAILDIR (%s) is not a directory with 'new' in it" % (os.environ["MAILDIR"],))
                   return
           maildir = os.environ["MAILDIR"]
    except Exception as e:
        logging.error("Exception: %s" % (e,))
        logging.error(traceback.format_exc())

    # Look for mbox...
    try:
        mbox = None
        if "MAIL" in os.environ and os.environ["MAIL"] and os.path.isfile(os.environ["MAIL"]):
            if os.path.isfile(os.environ["MAIL"]):
                mbox = os.environ["MAIL"]

        if mbox is None and LOGNAME in "os.environ" and os.environ["LOGNAME"] is not None:
            for m in ["/var/spool/mail", "/usr/spool/mail", "/var/mail", "/usr/mail"]:
                f = "%s/%s" % (m, os.environ["LOGNAME"])
                if os.path.isfile(f):
                    mbox = f
    except Exception as e:
        logging.error("Exception: %s" % (e,))
        logging.error(traceback.format_exc())

    try:
        # Check maildir if possible, and mbox if necessary
        if maildir is not None:
            check_mail("maildir %s" % (maildir,))
            epic.eval("^timer -refnum check_mail -repeat -1 -snap 60 check_mail maildir %s" % (maildir,))
        elif mbox is not None:
            check_mail("mbox %s" % (mbox,))
            epic.eval("^timer -refnum check_mail -repeat -1 -snap 60 check_mail mbox %s" % (mbox,))
        else:
            epic.xecho("I cannot find your mailbox.  Set the MAIL or MAILDIR env variable and try again.")
    except Exception as e:
        logging.error("Exception: %s" % (e,))
        logging.error(traceback.format_exc())


@epic.alias('set_mail_off')
def set_mail_off(args):
    try:
        epic.eval("^timer -delete -name check_mail")
    except Exception as e:
        logging.error("Exception: %s" % (e,))
        logging.error(traceback.format_exc())


