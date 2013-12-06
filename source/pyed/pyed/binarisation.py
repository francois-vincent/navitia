"""
Functions to launch the binaratisations
"""
from pyed.launch_exec import launch_exec
import logging
import subprocess
import os
import pyed.task_pb2
import zipfile
from pyed.app import celery, redis
import datetime
from flask import current_app
import kombu

def move_to_backupdirectory(filename, instance):
    """ If there is no backup directory it creates one in
        {instance_directory}/backup/{name}
        The name of the backup directory is the time when it's created
        formatted as %Y%m%d-%H%M%S
    """
    now = datetime.datetime.now()
    working_directory = instance.backup_directory
    working_directory += "/"+now.strftime("%Y%m%d-%H%M%S%f")
    os.mkdir(working_directory)
    destination = working_directory + '/' + os.path.basename(filename)
    os.rename(filename, destination)
    return destination

def make_connection_string(instance):
    """ Make a connection string connection from the config """
    connection_string = 'host=' + instance.pg_host
    connection_string += ' user=' + instance.pg_username
    connection_string += ' dbname=' + instance.pg_dbname
    connection_string += ' password=' + instance.pg_password
    return connection_string

@celery.task()
def fusio2ed(instance, filename):
    """ Unzip gtfs file, remove the file, launch gtfs2ed """

    lock = redis.lock('pyed.lock|' + instance.name)
    if not lock.acquire(blocking=False):
        gtfs2ed.retry(countdown=300, max_retries=10)

    try:
        pyed_logger = logging.getLogger('pyed')
        fusio_logger = logging.getLogger('fusio2ed')
        bnanme = os.path.basename(filename)
        working_directory = os.path.dirname(filename)

        os.rename(filename, filename)
        zip_file = zipfile.ZipFile(filename);
        zip_file.extractall(path=working_directory)

        params = ["-i", working_directory]
        if instance.aliases_file:
            params.append("-a")
            params.append(instance.aliases_file)

        if instance.synonyms_file:
            params.append("-s")
            params.append(instance.synonyms_file)

        connection_string = make_connection_string(instance)
        params.append("--connection-string")
        params.append(connection_string)
        res = launch_exec("fusio2ed", params, fusio_logger, pyed_logger)
        if res != 0:
            #@TODO: exception
            raise ValueError('todo: exception')
    finally:
        lock.release()


@celery.task()
def gtfs2ed(instance, gtfs_filename):
    """ Unzip gtfs file, remove the file, launch gtfs2ed """

    lock = redis.lock('pyed.lock|' + instance.name)
    if not lock.acquire(blocking=False):
        gtfs2ed.retry(countdown=300, max_retries=10)

    try:
        pyed_logger = logging.getLogger('pyed')
        gtfs_logger = logging.getLogger('gtfs2ed')
        gtfs_bnanme = os.path.basename(gtfs_filename)
        working_directory = os.path.dirname(gtfs_filename)

        os.rename(gtfs_filename, gtfs_filename)
        zip_file = zipfile.ZipFile(gtfs_filename);
        zip_file.extractall(path=working_directory)

        params = ["-i", working_directory]
        if instance.aliases_file:
            params.append("-a")
            params.append(instance.aliases_file)

        if instance.synonyms_file:
            params.append("-s")
            params.append(instance.synonyms_file)

        connection_string = make_connection_string(instance)
        params.append("--connection-string")
        params.append(connection_string)
        res = launch_exec("gtfs2ed", params, gtfs_logger, pyed_logger)
        if res != 0:
            #@TODO: exception
            raise ValueError('todo: exception')
    finally:
        lock.release()


@celery.task()
def osm2ed(instance, osm_filename):
    """ Move osm file to backup directory, launch osm2ed """

    lock = redis.lock('pyed.lock|' + instance.name)
    if not lock.acquire(blocking=False):
        osm2ed.retry(countdown=300, max_retries=10)

    try:
        pyed_logger = logging.getLogger('pyed')
        osm_logger = logging.getLogger('osm2ed')

        connection_string = make_connection_string(instance)
        res = launch_exec('osm2ed',
                ["-i", osm_filename, "--connection-string", connection_string],
                osm_logger, pyed_logger)
        if res != 0:
            #@TODO: exception
            raise ValueError('todo: exception')
    finally:
        lock.release()

@celery.task()
def reload_data(instance):
    """ reload data on all kraken"""
    task = pyed.task_pb2.Task()
    task.action = pyed.task_pb2.RELOAD
    pyed_logger = logging.getLogger('pyed')

    connection = kombu.Connection(current_app.config['CELERY_BROKER_URL'])
    exchange = kombu.Exchange(instance.exchange, 'topic', durable=True)
    producer = connection.Producer(exchange=exchange)

    pyed_logger.info("reload kraken")
    producer.publish(task.SerializeToString(),
            routing_key=instance.name+'.task.reload')
    connection.release()


@celery.task()
def ed2nav(instance):
    """ Launch ed2nav"""
    lock = redis.lock('pyed.lock|' + instance.name)
    if not lock.acquire(blocking=False):
        ed2nav.retry(countdown=300, max_retries=10)

    try:
        pyed_logger = logging.getLogger('pyed')
        ed2nav_logger = logging.getLogger('ed2nav')
        filename = instance.tmp_file
        connection_string = make_connection_string(instance)
        res = launch_exec('ed2nav',
                    ["-o", filename, "--connection-string", connection_string],
                    ed2nav_logger, pyed_logger)
        if res != 0:
            raise ValueError('todo: exception')
    finally:
        lock.release()

@celery.task()
def nav2rt(instance):
    """ Launch nav2rt"""
    lock = redis.lock('pyed.lock|' + instance.name)
    if not lock.acquire(blocking=False):
        nav2rt.retry(countdown=300, max_retries=10)

    try:
        pyed_logger = logging.getLogger('pyed')
        nav2rt_logger = logging.getLogger('nav2rt')
        source_filename = instance.tmp_file
        target_filename = instance.target_file
        connection_string = make_connection_string(instance)
        res = launch_exec('nav2rt',
                    ["-i", source_filename, "-o", target_filename,
                        "--connection-string", connection_string],
                    nav2rt_logger, pyed_logger)
        if res != 0:
            raise ValueError('todo: exception')
    finally:
        lock.release()
