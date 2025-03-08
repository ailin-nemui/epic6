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

####
class MAIL_subfactory(object):
    def __init__(self):
        pass
    def set_on(self, args):
        pass
    def set_off(self, args):
        pass
    def check(self):
        return []

class MBOX_factory(MAIL_subfactory):
    def __init__(self):
        self.valid = False

    def set_on(self, args):
        self.mbox = None

        # Look for mbox...
        if "MAIL" in os.environ and os.environ["MAIL"] and os.path.isfile(os.environ["MAIL"]):
            if os.path.isfile(os.environ["MAIL"]):
                self.mbox = os.environ["MAIL"]
                self.valid = True

        if self.mbox is None and LOGNAME in "os.environ" and os.environ["LOGNAME"] is not None:
            for m in ["/var/spool/mail", "/usr/spool/mail", "/var/mail", "/usr/mail"]:
                f = "%s/%s" % (m, os.environ["LOGNAME"])
                if os.path.isfile(f):
                    self.mbox = f
                    self.valid = True

    def set_off(self, args):
        self.valid = False

    def check(self):
        r = []
        if self.valid == True:
            m = mailbox.mbox(self.mbox)
            r = [hashlib.sha256(d["message-id"].encode("ISO-8859-1")).hexdigest() for d in m]
        return r

class MAILDIR_factory(MAIL_subfactory):
    def __init__(self):
        self.valid = False

    def set_on(self, args):
        self.maildir = None

        # Look for maildir...
        if "MAILDIR" in os.environ and os.environ["MAILDIR"] and os.path.isdir(os.environ["MAILDIR"]):
           for sd in ["new", "cur"]:
               d = "%s/%s" % (os.environ["MAILDIR"], sd)
               if not os.path.isdir(d):
                   epic.xecho("The environment variable MAILDIR (%s) is not a directory with 'new' in it" % (os.environ["MAILDIR"],))
                   return 
           self.maildir = os.environ["MAILDIR"]
           self.valid = True

    def set_off(self, args):
        self.valid = False

    def check(self):
        r = []
        if self.valid == True:
            m = mailbox.Maildir(self.maildir)
            r = [key for key,msg in m.iteritems() if "S" not in msg.get_flags()]
        return r

class MAIL_factory(MAIL_subfactory):
    def __init__(self):
        self.valid = False
        self.prev = []
        self.factories = []

    def add_factory(self, factory):
        try:
            self.factories.append(factory)
        except Exception as e:
            logging.error("Exception: %s" % (e,))
            logging.error(traceback.format_exc())

    def set_on(self, args):
        try:
            for factory in self.factories:
                factory.set_on(args)
            interval = epic.expression("MAIL_INTERVAL")
            epic.eval("timer -refnum check_mail -repeat -1 -snap %s check_mail" % (interval,))
        except Exception as e:
            logging.error("Exception: %s" % (e,))
            logging.error(traceback.format_exc())

    def set_off(self, args):
        try:
            for factory in self.factories:
                factory.set_off(args)
            epic.eval("timer -delete check_mail")
        except Exception as e:
            logging.error("Exception: %s" % (e,))
            logging.error(traceback.format_exc())

    def check(self):
        try:
            curr = []
            for factory in self.factories:
                curr = curr + list(factory.check())

            curr_count = len(curr)
            if curr_count == 0:
                epic.eval("^set -status_mail")
            else:
                new_count = 0
                for c in curr: 
                    if c not in self.prev:
                        new_count = new_count + 1

                self.prev = curr
                if (new_count > 0):
                    epic.xecho('There are %d new email(s), %d total' % (new_count, curr_count))

                strx = epic.expression("MAIL_STATUS_FORMAT")
                x = {"new": new_count, "cur": curr_count}
                final = strx.format(**x)
                epic.eval("^set status_mail %s " % (final,))
        except Exception as e:
            logging.error("Exception: %s" % (e,))
            logging.error(traceback.format_exc())

####
mail_factory = None

def establish_mail_factory():
    global mail_factory

    try:
        if mail_factory is None:
            mail_factory = MAIL_factory()
            mail_factory.add_factory(MAILDIR_factory())
            mail_factory.add_factory(MBOX_factory())
    except Exception as e:
        logging.error("Exception: %s" % (e,))
        logging.error(traceback.format_exc())

@epic.alias('check_mail')
def check_mail(args):
    global mail_factory

    try:
        establish_mail_factory()
        mail_factory.check()
    except Exception as e:
        logging.error("Exception: %s" % (e,))
        logging.error(traceback.format_exc())

@epic.alias('set_mail')
def set_mail(args):
    global mail_factory

    try:
        establish_mail_factory()

        if args is None: 
            return

        value = args.split()[0]
        if value == "ON":
           mail_factory.set_on(args)
           check_mail(args)
        else:
           mail_factory.set_off(args)
    except Exception as e:
        logging.error("Exception: %s" % (e,))
        logging.error(traceback.format_exc())

########
epic.eval("^addset mail bool {set_mail $*}")
epic.eval("^addset mail_interval int {set mail off;set mail on}")
epic.eval("^addset mail_status_format str")
epic.eval("^set mail_status_format (Mail: {cur})")
epic.eval("^set mail_interval 60")
epic.eval("^set mail on")

#hop'2k25
