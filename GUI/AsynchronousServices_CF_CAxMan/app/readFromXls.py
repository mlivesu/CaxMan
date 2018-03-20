#!/usr/bin/env python

import xlrd
from collections import OrderedDict
import simplejson as json
import sys
reload(sys)
sys.setdefaultencoding('utf-8')

def xls2JSON():
	# Open the workbook
	wb = xlrd.open_workbook('CAxManPrintingParameters.xlsx')
 
	# Print the sheet names
	print wb.sheet_names()
 
	# Get the first sheet either by index or by name
	sh = wb.sheet_by_index(0)

	#Gettting labels
	labels = sh.row_values(0)
	#print len(labels)
	#print type(labels)

	storeRowEntry = []

	for labelvalue in labels:
		print labelvalue

	#for colnum in range(sh.ncolumns)
	#print sh.col_values(colnum)

	#Creating JSON Objects 	
	for rownum in range(1, sh.nrows):
		#Templist stores value of each row as a list
		templist =  sh.row_values(rownum)
		createObj = OrderedDict()
		if(len(labels)==len(templist)):
			for itr in range(len(templist)):
				#createObj[labels[itr]] = str(templist[itr]).encode('ascii','ignore')
				createObj[labels[itr]] = str(templist[itr]).encode('ascii',errors='xmlcharrefreplace')
		
			storeRowEntry.append(createObj)
		
 


	print storeRowEntry

	#JSON obj
	jsonData = json.dumps(storeRowEntry)

	return jsonData
 
	#Checking by writing to a file
	#with open('fileobj.json', 'w') as f:
		#f.write(serialJSON)


	