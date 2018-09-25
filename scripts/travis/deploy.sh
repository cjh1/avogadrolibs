#!/bin/bash
sudo pip install twine
twine upload --repository-url  https://test.pypi.org/legacy/ dist/*
