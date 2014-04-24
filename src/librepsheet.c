/**
 * @file librepsheet.c
 * @author Aaron Bedra
 * @date 4/12/2014
 */

/*! \mainpage librepsheet
 *
 * \section intro_sec Introduction
 *
 * librepsheet is the core logic library for Repsheet. It is used in the Repsheet webserver modules as well as the backend.
 *
 * \section install_sec Installation
 *
 * librepsheet can be installed via your package manager of choice or compiled from source
 *
 * \subsection step1 From source
 *
 * TODO: write this
 */

#include "repsheet.h"

/**
 * This function establishes a connection to Redis
 *
 * @param host the hostname of the Redis server
 * @param port the port number of the Redis server
 * @param timeout the length in milliseconds before the connection times out
 *
 * @returns a Redis connection
 */
redisContext *get_redis_context(char *host, int port, int timeout)
{
  redisContext *context;

  struct timeval time = {0, (timeout > 0) ? timeout : 10000};

  context = redisConnectWithTimeout(host, port, time);
  if (context == NULL || context->err) {
    if (context) {
      printf("Error: %s\n", context->errstr);
      redisFree(context);
    } else {
      printf("Error: could not connect to Redis\n");
    }
    return NULL;
  } else {
    return context;
  }
}

/**
 * Increments the number of times a rule has been triggered for an actor
 *
 * @param context the Redis connection
 * @param actor the IP address of the actor
 * @param rule the ModSecurity rule number
 */
void increment_rule_count(redisContext *context, char *actor, char *rule)
{
  freeReplyObject(redisCommand(context, "ZINCRBY %s:detected 1 %s", actor, rule));
}

/**
 * Adds the actor to the Repsheet
 *
 * @param context the Redis connection
 * @param actor the IP address of the actor
 */
void mark_actor(redisContext *context, char *actor)
{
  freeReplyObject(redisCommand(context, "SET %s:repsheet true", actor));
}

/**
 * Adds the actor to the Repsheet blacklist
 *
 * @param context the Redis connection
 * @param actor the IP address of the actor
 */
void blacklist_actor(redisContext *context, char *actor)
{
  freeReplyObject(redisCommand(context, "SET %s:repsheet:blacklist true", actor));
}

/**
 * Adds the actor to the Repsheet whitelist
 *
 * @param context the Redis connection
 * @param actor the IP address of the actor
 */
void whitelist_actor(redisContext *context, char *actor)
{
  freeReplyObject(redisCommand(context, "SET %s:repsheet:whitelist true", actor));
}

/**
 * Checks to see if an actor is on the Repsheet
 *
 * @param context the Redis connection
 * @param actor the IP address of the actor
 *
 * @returns TRUE if yes, FALSE if no
 */
int is_on_repsheet(redisContext *context, char *actor)
{
  redisReply *reply;

  reply = redisCommand(context, "GET %s:repsheet", actor);
  if (reply->str && strcmp(reply->str, "true") == 0) {
    freeReplyObject(reply);
    return TRUE;
  }

  return FALSE;
}

/**
 * Checks to see if an actor is on the Repsheet blacklist
 *
 * @param context the Redis connection
 * @param actor the IP address of the actor
 *
 * @returns TRUE if yes, FALSE if no
 */
int is_blacklisted(redisContext *context, char *actor)
{
  redisReply *reply;

  reply = redisCommand(context, "GET %s:repsheet:blacklist", actor);
  if (reply->str && strcmp(reply->str, "true") == 0) {
    freeReplyObject(reply);
    return TRUE;
  }

  return FALSE;
}

/**
 * Checks to see if an actor is on the Repsheet whitelist
 *
 * @param context the Redis connection
 * @param actor the IP address of the actor
 *
 * @returns TRUE if yes, FALSE if no
 */
int is_whitelisted(redisContext *context, char *actor)
{
  redisReply *reply;

  reply = redisCommand(context, "GET %s:repsheet:whitelist", actor);
  if (reply->str && strcmp(reply->str, "true") == 0) {
    freeReplyObject(reply);
    return TRUE;
  }

  return FALSE;
}

/**
 * Sets the expiry for a record
 *
 * @param context the Redis connection
 * @param actor the IP address of the actor
 * @param label the label associated with the actor
 * @param expiry the length until the record expires
 */
void expire(redisContext *context, char *actor, char *label, int expiry)
{
  freeReplyObject(redisCommand(context, "EXPIRE %s:%s %d", actor, label, expiry));
}

/**
 * Blacklists and sets the expiry for a record. Adds the actor to the
 * blacklist history and sets the reason code.
 *
 * @param context the Redis connection
 * @param actor the IP address of the actor
 * @param expiry the length until the record expires
 * @param reason the reason for blacklisting the actor
 */
void blacklist_and_expire(redisContext *context, char *actor, int expiry, char *reason)
{
  freeReplyObject(redisCommand(context, "SETEX %s:repsheet:blacklist %d true", actor, expiry));
  freeReplyObject(redisCommand(context, "SETEX %s:repsheet:blacklist:reason %d %s", actor, expiry, reason));
  freeReplyObject(redisCommand(context, "SADD repsheet:blacklist:history %s", actor));
}

/**
 * Records details about the request and stores them in Redis
 *
 * @param context the redis connection
 * @param timestamp the request timestamp
 * @param user_agent the requets user agent string
 * @param http_method the http method
 * @param uri the endpoint for the request
 * @param arguments the query string
 * @param redis_max_length the maximum number of requests to keep in Redis
 * @param redis_expiry the time to live for the entry
 * @param actor the ip address of the actor
 */
void record(redisContext *context, char *timestamp, const char *user_agent,
	    const char *http_method, char *uri, char *arguments, int redis_max_length,
	    int redis_expiry, const char *actor)
{
  char *t, *ua, *method, *u, *args, *rec;

  //TODO: should probably use asprintf here to save a bunch of
  //nonsense calls. Make sure there is a sane way to do this across
  //platforms.

  if (timestamp == NULL) {
    t = malloc(2);
    sprintf(t, "%s", "-");
  } else {
    t = malloc(strlen(timestamp) + 1);
    sprintf(t, "%s", timestamp);
  }

  if (user_agent == NULL) {
    ua = malloc(2);
    sprintf(ua, "%s", "-");
  } else {
    ua = malloc(strlen(user_agent) + 1);
    sprintf(ua, "%s", user_agent);
  }

  if (http_method == NULL) {
    method = malloc(2);
    sprintf(method, "%s", "-");
  } else {
    method = malloc(strlen(http_method) + 1);
    sprintf(method, "%s", http_method);
  }

  if (uri == NULL) {
    u = malloc(2);
    sprintf(u, "%s", "-");
  } else {
    u = malloc(strlen(uri) + 1);
    sprintf(u, "%s", uri);
  }

  if (arguments == NULL) {
    args = malloc(2);
    sprintf(args, "%s", "-");
  } else {
    args = malloc(strlen(arguments) + 1);
    sprintf(args, "%s", arguments);
  }

  rec = (char*)malloc(strlen(t) + strlen(ua) + strlen(method) + strlen(u) + strlen(args) + 9);
  sprintf(rec, "%s, %s, %s, %s, %s", t, ua, method, u, args);

  freeReplyObject(redisCommand(context, "LPUSH %s:requests %s", actor, rec));
  freeReplyObject(redisCommand(context, "LTRIM %s:requests 0 %d", actor, (redis_max_length - 1)));
  if (redis_expiry > 0) {
    freeReplyObject(redisCommand(context, "EXPIRE %s:requests %d", actor, redis_expiry));
  }
}
