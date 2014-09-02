#!/usr/bin/env python
#
#  Copyright (C) 2013 Cloudius Systems, Ltd.
#
#  This work is open source software, licensed under the terms of the
#  BSD license as described in the LICENSE file in the top-level directory.
#
#  This is a script to cleanup unused objects from AWS account.
#  This script is handling following entities:
#    EC2: Snapshots, volumes, AMIs, instances
#    S3: Buckets
#
#  Rules of handling are slightly differ depending on the entity type:
#    Snapshots, Volumes and Buckets:
#      1. If entity has tag "permanent" with any value
#         it never touched by this script.
#      2. If entity has tag "max_life_time" its value treated as number
#         of hours the entity is entitled to exist.
#         If entity is older - script deletes it.
#      3. If entity has no tags listed above and older than 6 hours -
#         script deletes it.
#    Instances:
#      Rules are the same as above but the script stops
#      instances instead of deleting.
#    AMIs:
#      Since AWS doesn't track AMI creation date script deletes all AMIs
#      that do not have "permanent" tag.
#
#    Script configuration:
#      AWS credentials passed via environment
#      variables AWS_ACCESS_KEY_ID and AWS_SECRET_ACCESS_KEY.

import boto
import boto.ec2
import os
import time
import calendar

ACCESS_KEY=os.environ['AWS_ACCESS_KEY_ID']
SECRET_KEY=os.environ['AWS_SECRET_ACCESS_KEY']

MAX_OBJECT_LIFE_TIME_HRS=6
DRY_RUN=False

class BotoObject:
  def __init__(self, boto_object):
    self.boto_object = boto_object

  def name(self):
    if 'Name' in self.boto_object.tags:
      return self.boto_object.tags['Name']
    else:
      return "unnamed"

  def permanent(self):
    return 'permanent' in self.boto_object.tags

  def time_since_amazon_time(self, amazon_time):
    now_utc=time.gmtime(time.time())
    start_utc=time.strptime(amazon_time[:19],'%Y-%m-%dT%H:%M:%S')
    return float(calendar.timegm(now_utc) - calendar.timegm(start_utc))

  def get_tag(self, tag_name):
    try:
      if tag_name in self.boto_object.tags:
        return self.boto_object.tags[tag_name]
    except:
      pass
    return None

  def max_life_time(self):
    time_hrs = self.get_tag('max_life_time')
    if time_hrs:
      return int(time_hrs) * 3600
    else:
      return MAX_OBJECT_LIFE_TIME_HRS * 3600

  def too_old(self):
    if self.permanent():
      return False
    return self.life_time() > self.max_life_time()

class Snapshot(BotoObject):
  def __init__(self, boto_snapshot):
    self.boto_snapshot = boto_snapshot
    BotoObject.__init__(self, boto_snapshot)

  def life_time(self):
    return BotoObject.time_since_amazon_time(self, self.boto_snapshot.start_time)

  def delete(self):
    if not DRY_RUN:
      try:
        self.boto_snapshot.delete()
      except:
        print "The snapshot is in use."

  def old_and_unused(self):
    if self.boto_snapshot.status != 'completed':
      return False
    return BotoObject.too_old(self)

class Volume(BotoObject):
  def __init__(self, boto_volume):
    self.boto_volume = boto_volume
    BotoObject.__init__(self, boto_volume)

  def life_time(self):
    return BotoObject.time_since_amazon_time(self, self.boto_volume.create_time)

  def old_and_unused(self):
    if self.boto_volume.status != 'available':
      return False
    return BotoObject.too_old(self)

  def delete(self):
    if not DRY_RUN:
      self.boto_volume.delete()

class Bucket(BotoObject):
  def __init__(self, boto_bucket):
    self.boto_bucket = boto_bucket
    BotoObject.__init__(self, boto_bucket)

  def life_time(self):
    return BotoObject.time_since_amazon_time(self, self.boto_bucket.creation_date)

  def delete(self):
    if not DRY_RUN:
      for key in self.boto_bucket.list():
        self.boto_bucket.delete_key(key)
      self.boto_bucket.delete()

  def name(self):
    return self.boto_bucket.name

  def get_tag(self, tag_name):
    try:
      tags = self.boto_object.get_tags()
      for subtags in tags:
        for subtag in subtags:
          if subtag.key == tag_name:
            return subtag.value
    except:
      pass
    return None

  def permanent(self):
    return self.get_tag('permanent')

class Image(BotoObject):
  def __init__(self, boto_image):
    self.boto_image = boto_image
    BotoObject.__init__(self, boto_image)

  def deregister(self):
    if not DRY_RUN:
      self.boto_image.deregister(True)

class Instance(BotoObject):
  def __init__(self, boto_instance):
    self.boto_instance = boto_instance
    BotoObject.__init__(self, boto_instance)

  def life_time(self):
    return BotoObject.time_since_amazon_time(self, self.boto_instance.launch_time)

  def runs_for_too_long(self):
    if self.boto_instance.state != 'running':
      return False
    return BotoObject.too_old(self)

  def stop(self):
    if not DRY_RUN:
      self.boto_instance.stop()

def process_instance( instance ):
  if instance.boto_instance.state == 'running':
    print "Running instance found: %s (%s)" % (instance.boto_instance.id, instance.name())
    print "Run time: %d hour(s)" % (instance.life_time() / 3600)
    if instance.runs_for_too_long():
      print "Stopping instance %s" % instance.boto_instance.id
      instance.stop()

def process_image( image ):
  if not image.permanent():
    print "Deregistering AMI %s (%s)" % (image.boto_image.id, image.name())
    image.deregister()

def process_volume( volume ):
  if volume.old_and_unused():
    print "Deleting volume %s (%s)" % (volume.boto_volume.id, volume.name())
    volume.delete()

def process_snapshot( snapshot ):
  if snapshot.old_and_unused():
    print "Deleting snapshot %s (%s)" % (snapshot.boto_snapshot.id, snapshot.name())
    snapshot.delete()

def process_bucket( bucket ):
  if bucket.too_old():
    print "Deleting bucket %s" % (bucket.name())
    bucket.delete()

def process_region( region ):
  print "Processing region %s" % region.name

  ec2 = region.connect()

  print "Scanning instances...\n"

  instances = ec2.get_only_instances()
  for inst in instances:
    inst_accessor = Instance(inst)
    process_instance(inst_accessor)

  print "\nScanning images...\n"

  images = ec2.get_all_images(None, ('self'))
  for image in images:
    image_accessor = Image(image)
    process_image(image_accessor)

  print "\nScanning volumes...\n"

  volumes = ec2.get_all_volumes()
  for volume in volumes:
    volume_accessor = Volume(volume)
    process_volume(volume_accessor)

  print "\nScanning snapshots...\n"

  snapshots = ec2.get_all_snapshots(owner='self')
  for snapshot in snapshots:
    process_snapshot(Snapshot(snapshot))

conn = boto.connect_ec2(ACCESS_KEY, SECRET_KEY)
regions = boto.ec2.regions()

for region in regions:
  try:
    process_region(region)
  except:
    print "\nFailed to process region %s\n" % region.name

from boto.s3.connection import OrdinaryCallingFormat
s3  = boto.connect_s3(ACCESS_KEY, SECRET_KEY, calling_format=OrdinaryCallingFormat())

print "\nScanning buckets...\n"

buckets = s3.get_all_buckets()
for bucket in buckets:
  process_bucket(Bucket(bucket))
