#!/bin/sh

# JULEA - Flexible storage framework
# Copyright (C) 2013-2017 Michael Kuhn
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

set -e

SELF_PATH="$(readlink --canonicalize-existing -- "$0")"
SELF_DIR="${SELF_PATH%/*}"
SELF_BASE="${SELF_PATH##*/}"

. "${SELF_DIR}/common"

usage ()
{
	echo "Usage: ${0##*/} start|stop|restart"
	exit 1
}

get_config ()
{
	local config_dir
	local config_name

	config_name='julea'

	if test -n "${JULEA_CONFIG}"
	then
		if test -f "${JULEA_CONFIG}"
		then
			cat "${JULEA_CONFIG}"
			return 0
		else
			config_name="${JULEA_CONFIG}"
		fi
	fi

	if test -f "${XDG_CONFIG_HOME:-${HOME}/.config}/julea/${config_name}"
	then
		cat "${XDG_CONFIG_HOME:-${HOME}/.config}/julea/${config_name}"
		return 0
	fi

	for config_dir in $(echo "${XDG_CONFIG_DIRS:-/etc/xdg}" | sed 's/:/ /g')
	do
		if test -f "${config_dir}/julea/${config_name}"
		then
			cat "${config_dir}/julea/${config_name}"
			return 0
		fi
	done

	return 1
}

get_host ()
{
	local server
	local host

	server="$1"

	host="${server%:*}"
	host="${host%.*}"

	printf '%s' "${host}"
}

get_port ()
{
	local server
	local port

	server="$1"

	port="${server#*:}"

	if test "${port}" = "${server}"
	then
		port=''
	fi

	printf '%s' "${port}"
}

do_start ()
{
	local host
	local server
	local port
	local port_option

	for server in ${SERVERS_OBJECT}
	do
		host="$(get_host "${server}")"
		port="$(get_port "${server}")"

		if test "${host}" = "${HOSTNAME}"
		then
			port_option=''
			test -n "${port}" && port_option="--port ${port}"

			julea-server --daemon ${port_option}
		fi
	done

	for server in ${SERVERS_KV}
	do
		host="$(get_host "${server}")"
		port="$(get_port "${server}")"

		if test "${host}" = "${HOSTNAME}"
		then
			# FIXME else
			if test "${KV_BACKEND}" = 'mongodb'
			then
				mkdir --parents "${MONGO_PATH}/db"
				mongod --fork --logpath "${MONGO_PATH}/mongod.log" --logappend --dbpath "${MONGO_PATH}/db" --journal
			fi
		fi
	done

	# Give the servers enough time to initialize properly.
	sleep 5
}

do_stop ()
{
	local host
	local port
	local server

	for server in ${SERVERS_OBJECT}
	do
		host="$(get_host "${server}")"
		port="$(get_port "${server}")"

		if test "${host}" = "${HOSTNAME}"
		then
			killall --verbose julea-server || true
			rm -rf "${OBJECT_PATH}"
		fi
	done

	for server in ${SERVERS_KV}
	do
		host="$(get_host "${server}")"
		port="$(get_port "${server}")"

		if test "${host}" = "${HOSTNAME}"
		then
			if test "${KV_BACKEND}" = 'mongodb'
			then
				mongod --shutdown --dbpath "${MONGO_PATH}/db" || true
				rm -rf "${MONGO_PATH}"
			else
				rm -rf "${KV_PATH}"
			fi
		fi
	done
}

test -n "$1" || usage

MODE="$1"

HOSTNAME="$(hostname)"
USER="$(id --name --user)"

MONGO_PATH="/tmp/julea-mongo-${USER}"

set_path
set_library_path

SERVERS_OBJECT="$(get_config | grep ^object=)"
SERVERS_KV="$(get_config | grep ^kv=)"
OBJECT_BACKEND="$(get_config | grep ^backend= | head --lines=1)"
KV_BACKEND="$(get_config | grep ^backend= | tail --lines=1)"
OBJECT_PATH="$(get_config | grep ^path= | head --lines=1)"
KV_PATH="$(get_config | grep ^path= | tail --lines=1)"

SERVERS_OBJECT="${SERVERS_OBJECT#object=}"
SERVERS_KV="${SERVERS_KV#kv=}"
OBJECT_BACKEND="${OBJECT_BACKEND#backend=}"
KV_BACKEND="${KV_BACKEND#backend=}"
OBJECT_PATH="${OBJECT_PATH#path=}"
KV_PATH="${KV_PATH#path=}"

SERVERS_OBJECT="${SERVERS_OBJECT%;}"
SERVERS_KV="${SERVERS_KV%;}"

SERVERS_OBJECT=$(echo ${SERVERS_OBJECT} | sed 's/;/ /g')
SERVERS_KV=$(echo ${SERVERS_KV} | sed 's/;/ /g')

case "${MODE}" in
	start)
		do_start
		;;
	stop)
		do_stop
		;;
	restart)
		do_stop
		sleep 10
		do_start
		;;
	*)
		usage
		;;
esac
