#!/usr/bin/python
#
# Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
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
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.


import sys
import json
import requests
import os.path
import re

shaderId = sys.argv[1]
idMatch = re.match(r"[a-zA-Z0-9]+$", shaderId)
urlMatch = re.match(r"https://www.shadertoy.com/view/([a-zA-Z0-9]+)$", shaderId)

if urlMatch:
	shaderId = urlMatch.group(1)
elif not idMatch:
	print("ERROR: Unrecognized format for the shader ID or URL")
	sys.exit(1)


url = "https://www.shadertoy.com/shadertoy"
postdata = 's={ "shaders" : ["' + shaderId + '"] }'

headers = {
	'Content-Type': 'application/x-www-form-urlencoded',
	'Referer': 'https://www.shadertoy.com/browse',
	'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)'
}

response = requests.post(url, data = postdata, headers = headers)
if not response:
	print(response)
	sys.exit(1)

if len(response.content) == 0:
	print("ERROR: Empty HTTP response received.")
	sys.exit(1)

content = json.loads(response.content)

if len(sys.argv) > 2:
	outputPath = sys.argv[2]
else:
	outputPath = shaderId

os.makedirs(outputPath, exist_ok = True)

for renderpass in content[0]["renderpass"]:
	for input in renderpass["inputs"]:
		filepath = input["filepath"] 
		print(filepath)
		if not filepath.startswith('/media'):
			print("skipping")
			continue
		outfile = outputPath + '/..' + filepath
		os.makedirs(os.path.dirname(outfile), exist_ok = True)
		url = "http://shadertoy.com" + filepath
		if not os.path.exists(outfile):
			response = requests.get(url, headers = headers)
			if response:
				with open(outfile, 'wb') as f:
					f.write(response.content)
			else:
				print(response)
	
	code = renderpass["code"] + '\n'
	passname = renderpass["name"].replace(' ', '') + ".glsl"

	codefile = outputPath + '/' + passname
	renderpass["code"] = passname
	with open(codefile, 'wb') as f:
		f.write(code.encode('ascii', 'ignore'))

with open(outputPath + '/description.json', "w") as f:
	f.write(json.dumps(content, indent = 2))
