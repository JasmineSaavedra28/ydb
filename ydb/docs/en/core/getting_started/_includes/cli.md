# {{ ydb-short-name }} CLI - Getting started

## Prerequisites {#prerequisites}

To run commands via the CLI, you will need database connection settings you can get when [creating](../create_db.md) a database:

* [Endpoint](../../concepts/connect.md#endpoint)
* [Database name](../../concepts/connect.md#database)

You may also need a token or login/password if the database requires [authentication](../auth.md). To execute the below scenario, you need to select an option for saving them in an environment variable.

## Installing the CLI {#install}

Install the {{ ydb-short-name }} CLI as described in [Installing the {{ ydb-short-name }} CLI](../../reference/ydb-cli/install.md).

To check that the YDB CLI has been installed, run it with `--help`:

```bash
{{ ydb-cli }} --help
```

The response includes a welcome message, a brief description of the syntax, and a list of available commands:

```text
YDB client

Usage: ydb [options...] <subcommand>

Subcommands:
ydb
├─ config                   Manage YDB CLI configuration
│  └─ profile               Manage configuration profiles
│     ├─ activate           Activate specified configuration profile (aliases: set)
...
```

All the features of the {{ ydb-short-name }} CLI built-in help are described in [Built-in help](../../reference/ydb-cli/commands/service.md#help) of the {{ ydb-short-name }} CLI reference.

## Check the connection {#ping} {#scheme-ls}

To test connection, you can use the command for [listing objects](../../reference/ydb-cli/commands/scheme-ls.md) in the database, `scheme ls`:

```bash
{{ ydb-cli }} -e <endpoint> -d <database> scheme ls
```

If the command is successful, a list of objects in the database is shown in response. If you haven't created anything in the database yet, the output will only contain the `.sys` and `.sys_health` system directories with [diagnostic views of YDB](../../troubleshooting/system_views_db.md).

{% include [cli/ls_examples.md](cli/ls_examples.md) %}

## Creating a connection profile {#profile}

To avoid specifying connection parameters every time you call the YDB CLI, use the [profile](../../reference/ydb-cli/profile/index.md). Creating the profile described below will also let you copy subsequent commands through the clipboard without editing them regardless of which database you're using to complete the "Getting started" scenario.

[Create the profile](../../reference/ydb-cli/profile/create.md) `db1` using the following command:

```bash
{{ ydb-cli }} config profile create db1
```

You will be interactively prompted for connection parameters to be linked with the profile. Use for them the values verified in the [previous step](#ping).

Check that the profile is OK with the `scheme ls` command:

```bash
{{ ydb-cli }} -p db1 scheme ls
```

## Executing an YQL script {#yql}

The {{ ydb-short-name }} CLI `yql` command lets you execute any command (both DDL and DML) in [YQL](../../yql/reference/index.md), an SQL dialect supported by {{ ydb-short-name }}:

```bash
{{ ydb-cli }} -p <profile_name> yql -s <yql_request>
```

For example:

* Creating a table:

  ```bash
  {{ ydb-cli }} -p db1 yql -s "create table t1( id uint64, primary key(id))"
  ```

* Adding a record:

  ```bash
  {{ ydb-cli }} -p db1 yql -s "insert into t1(id) values (1)"
  ```

* Data selects:

  ```bash
  {{ ydb-cli }} -p db1 yql -s "select * from t1"
  ```

If you get the `Profile db1 does not exist` error, that means you neglected to create a profile in the [previous step](#profile).

## Specialized CLI commands {#ydb-api}

Executing commands via `ydb yql` is a nice and easy way to get started. However, the YQL interface supports a part of the function set provided by the YDB API, and has to sacrifice efficiency for universality.

The YDB CLI supports individual commands with complete sets of options for any existing YDB API. For a full list of commands, see the [YDB CLI reference](../../reference/ydb-cli/index.md).

## Next step {#next}

Go to [YQL - Getting started](../yql.md) to proceed with the 'Getting started' scenario.

