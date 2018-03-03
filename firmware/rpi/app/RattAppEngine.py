# -*- coding: utf-8 -*-
# --------------------------------------------------------------------------
#  _____       ______________
# |  __ \   /\|__   ____   __|
# | |__) | /  \  | |    | |
# |  _  / / /\ \ | |    | |
# | | \ \/ ____ \| |    | |
# |_|  \_\/    \_\_|    |_|    ... RFID ALL THE THINGS!
#
# A resource access control and telemetry solution for Makerspaces
#
# Developed at MakeIt Labs - New Hampshire's First & Largest Makerspace
# http://www.makeitlabs.com/
#
# Copyright 2018 MakeIt Labs
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
# WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
# --------------------------------------------------------------------------
#
# Author: Steve Richardson (steve.richardson@makeitlabs.com)
#

from PyQt5.QtCore import pyqtSlot, pyqtSignal, QVariant
from PyQt5 import QtCore
from PyQt5.QtQml import QQmlApplicationEngine, qmlRegisterType
from RattConfig import RattConfig
from NetWorker import NetWorker
from RFID import RFID
from Logger import Logger
import sys

class RattAppEngine(QQmlApplicationEngine):

    def __init__(self):
        QQmlApplicationEngine.__init__(self)

        # load the config
        self.config = RattConfig()

        # create parent logger which will optionally log to file and console, and opionally enable Qt logging
        self.logger = Logger(name='ratt',
                             filename=self.config.value('Log.File'), stream=self.config.value('Log.Console'),
                             qt=self.config.value('Log.Qt'), qtVerbose=self.config.value('Log.QtVerbose'))
        self.logger.setLogLevelStr(self.config.value('Log.LogLevel'))
        self.logger.info('Initializing RATT System')
        self.debug = self.logger.isDebug()

        # initialize the node personality and the other necessary modules
        self.__initSystem__()
        self.__initPersonality__()

        # create context properties so certain objects can be accessed from QML
        self.rootContext().setContextProperty("appEngine", self)
        self.rootContext().setContextProperty("config", self.config)
        self.rootContext().setContextProperty("personality", self.personality)
        self.rootContext().setContextProperty("netWorker", self._netWorker)
        self.rootContext().setContextProperty("rfid", self._rfid)

        # temporary for test; will move to a state machine eventually
        self._netWorker.fetchAcl()

        # begin executing the personality state machine
        self.personality.execute()

    def __initPersonality__(self):
        # dynamically import and instantiate the correct 'Personality' class, which contains the specific logic
        # implementation for a given tool
        personalityClass = 'Personality' + self.config.value('Personality.Class')

        try:
            sys.path.append('personalities')
            module = __import__(personalityClass)

            self.personality = module.Personality(loglevel=self.config.value('Personality.LogLevel'), app=self)
        except:
            self.logger.exception('could not establish personality: ' + personalityClass)
            exit(-1)


    def __initSystem__(self):
        # NetWorker handles fetching and maintaining ACLs, logging, and other network functions
        self._netWorker = NetWorker(loglevel=self.config.value('Auth.LogLevel'))
        self._netWorker.setSSLCertConfig(enabled=self.config.value('SSL.Enabled'),
                                         caCertFile=self.config.value('SSL.CaCertFile'),
                                         clientCertFile=self.config.value('SSL.ClientCertFile'),
                                         clientKeyFile=self.config.value('SSL.ClientKeyFile'))

        self._netWorker.setAuth(user=self.config.value('Auth.HttpAuthUser'),
                                password=self.config.value('Auth.HttpAuthPassword'))

        self._netWorker.setUrls(acl=self.config.value('Auth.AclUrl'),
                                log=self.config.value('Auth.LogUrl'))

        # RFID reader
        self._rfid = RFID(portName=self.config.value('RFID.SerialPort'),
                         loglevel=self.config.value('RFID.LogLevel'))

        self._rfid.monitor()

    def rfid(self):
        return self._rfid

    def netWorker(self):
        return self._netWorker




