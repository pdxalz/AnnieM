#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/random/rand32.h>
#include <zephyr/net/mqtt.h>
#include <nrf_modem_at.h>
#include <zephyr/logging/log.h>
#include "mqtt_connection.h"
#include "cameraThread.h"

/* Buffers for MQTT client. */
static uint8_t rx_buffer[CONFIG_MQTT_MESSAGE_BUFFER_SIZE];
static uint8_t tx_buffer[CONFIG_MQTT_MESSAGE_BUFFER_SIZE];
static uint8_t payload_buf[CONFIG_MQTT_PAYLOAD_BUFFER_SIZE];

/* MQTT Broker details. */
static struct sockaddr_storage broker;

/* The mqtt client struct */
static struct mqtt_client client;
/* File descriptor */
static struct pollfd fds;

// LOG_MODULE_DECLARE(AnnieM);
LOG_MODULE_REGISTER(mqtt_con, LOG_LEVEL_INF);

#define WAKEY_MODE "wake"
#define SAMPLE_FAST "fast"
#define SAMPLE_SLOW "slow"
#define REPORT "report"


uint8_t _mqtt_message_buf[200];
uint8_t _mqtt_topic_buf[80];

uint8_t * get_mqtt_message_buf()
{
	return _mqtt_message_buf;
}

uint8_t * get_mqtt_topic_buf()
{
	return _mqtt_topic_buf;
}


/**@brief Function to get the payload of recived data.
 */
static int get_received_payload(struct mqtt_client *c, size_t length)
{
	int ret;
	int err = 0;

	/* Return an error if the payload is larger than the payload buffer.
	 * Note: To allow new messages, we have to read the payload before returning.
	 */
	if (length > sizeof(payload_buf))
	{
		err = -EMSGSIZE;
	}

	/* Truncate payload until it fits in the payload buffer. */
	while (length > sizeof(payload_buf))
	{
		ret = mqtt_read_publish_payload_blocking(
			c, payload_buf, (length - sizeof(payload_buf)));
		if (ret == 0)
		{
			return -EIO;
		}
		else if (ret < 0)
		{
			return ret;
		}

		length -= ret;
	}

	ret = mqtt_readall_publish_payload(c, payload_buf, length);
	if (ret)
	{
		return ret;
	}

	return err;
}

/**@brief Function to subscribe to the configured topic
 */
/* STEP 4 - Define the function subscribe() to subscribe to a specific topic.  */
static int subscribe(struct mqtt_client *const c)
{
	struct mqtt_topic subscribe_topic = {
		.topic = {
			.utf8 = CONFIG_MQTT_CMD_TOPIC,
			.size = strlen(CONFIG_MQTT_CMD_TOPIC)},
		.qos = MQTT_QOS_1_AT_LEAST_ONCE};
	const struct mqtt_subscription_list subscription_list = {
		.list = &subscribe_topic,
		.list_count = 1,
		.message_id = 1234};
	LOG_INF("Subscribing to: %s len %u\n", CONFIG_MQTT_CMD_TOPIC,
			(unsigned int)strlen(CONFIG_MQTT_CMD_TOPIC));
	return mqtt_subscribe(c, &subscription_list);
}

/**@brief Function to print strings without null-termination
 */
static void data_print(uint8_t *prefix, uint8_t *data, size_t len)
{
	char buf[len + 1];

	memcpy(buf, data, len);
	buf[len] = 0;
	printk("%s%s\n", (char *)prefix, (char *)buf);
}

/**@brief Function to publish data on the configured topic
 */
/* STEP 7.1 - Define the function data_publish() to publish data */
int data_publish(enum mqtt_qos qos,
				 uint8_t *data, size_t len, uint8_t *topic, uint8_t retain)
{
	if (len > CONFIG_MQTT_MESSAGE_BUFFER_SIZE)
	{
		LOG_ERR("_mqtt_message_buf overflow: %d\n", len);
		len = CONFIG_MQTT_MESSAGE_BUFFER_SIZE - 1;
	}
	struct mqtt_publish_param param;
	param.message.topic.qos = qos;
	param.message.topic.topic.utf8 = topic; // CONFIG_MQTT_PUB_TOPIC;
	param.message.topic.topic.size = strlen(topic);
	param.message.payload.data = data;
	param.message.payload.len = len;
	param.message_id = sys_rand32_get();
	param.dup_flag = 0;
	param.retain_flag = retain;
	if (len > 2)
	{
//		data_print("Pub: ", data, len);
	}
	//	printk("to topic: %s len: %u\n", topic, (unsigned int)strlen(topic));
	return mqtt_publish(&client, &param);
}

/**@brief MQTT client event handler
 */
void mqtt_evt_handler(struct mqtt_client *const c,
					  const struct mqtt_evt *evt)
{
	int err;

	switch (evt->type)
	{
	case MQTT_EVT_CONNACK:
		if (evt->result != 0)
		{
			LOG_WRN("MQTT connect failed: %d\n", evt->result);
			break;
		}
		LOG_INF("MQTT client connected\n");
		subscribe(c);
		break;

	case MQTT_EVT_DISCONNECT:
		LOG_WRN("MQTT client disconnected: %d\n", evt->result);
		break;

	case MQTT_EVT_PUBLISH:
		/* STEP 6 - Listen to published messages received from the broker and extract the message */
		{
			/* STEP 6.1 - Extract the payload */
			const struct mqtt_publish_param *p = &evt->param.publish;
			// Print the length of the recived message
			// LOG_INF("MQTT PUBLISH result=%d len=%d\n", evt->result, p->message.payload.len);
			// Extract the data of the recived message
			err = get_received_payload(c, p->message.payload.len);
			// Send acknowledgment to the broker on receiving QoS1 publish message
			if (p->message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE)
			{
				const struct mqtt_puback_param ack = {
					.message_id = p->message_id};
				/* Send acknowledgment. */
				mqtt_publish_qos1_ack(c, &ack);
			}
			/* STEP 6.2 - On successful extraction of data */
			// On successful extraction of data
			if (err >= 0)
			{
				// data_print("Received: ", payload_buf, p->message.payload.len);
				cameraCommand(payload_buf);
			}
			/* STEP 6.3 - On failed extraction of data */
			// On failed extraction of data - Payload buffer is smaller than the recived data . Increase
			else if (err == -EMSGSIZE)
			{
				LOG_WRN("Received payload (%d bytes) is larger than the payload buffer size (%d bytes).\n",
						p->message.payload.len, sizeof(payload_buf));
				// On failed extraction of data - Failed to extract data, disconnect
			}
			else
			{
				LOG_WRN("get_received_payload failed: %d\n", err);
				LOG_WRN("Disconnecting MQTT client...\n");
				err = mqtt_disconnect(c);
				if (err)
				{
					LOG_WRN("Could not disconnect: %d\n", err);
				}
			}
		}
		break;

	case MQTT_EVT_PUBACK:
		if (evt->result != 0)
		{
			LOG_WRN("MQTT PUBACK error: %d\n", evt->result);
			break;
		}

		//		printk("PUBACK packet id: %u\n", evt->param.puback.message_id);
		break;

	case MQTT_EVT_SUBACK:
		if (evt->result != 0)
		{
			LOG_WRN("MQTT SUBACK error: %d\n", evt->result);
			break;
		}

		LOG_INF("SUBACK packet id: %u\n", evt->param.suback.message_id);
		break;

	case MQTT_EVT_PINGRESP:
		if (evt->result != 0)
		{
			LOG_WRN("MQTT PINGRESP error: %d\n", evt->result);
		}
		break;

	default:
		LOG_WRN("Unhandled MQTT event type: %d\n", evt->type);
		break;
	}
}

/**@brief Resolves the configured hostname and
 * initializes the MQTT broker structure
 */
static int broker_init(void)
{
	int err;
	struct addrinfo *result;
	struct addrinfo *addr;
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM};

	err = getaddrinfo(CONFIG_MQTT_BROKER_HOSTNAME, NULL, &hints, &result);
	if (err)
	{
		LOG_WRN("getaddrinfo failed: %d\n", err);
		return -ECHILD;
	}

	addr = result;

	/* Look for address of the broker. */
	while (addr != NULL)
	{
		/* IPv4 Address. */
		if (addr->ai_addrlen == sizeof(struct sockaddr_in))
		{
			struct sockaddr_in *broker4 =
				((struct sockaddr_in *)&broker);
			char ipv4_addr[NET_IPV4_ADDR_LEN];

			broker4->sin_addr.s_addr =
				((struct sockaddr_in *)addr->ai_addr)
					->sin_addr.s_addr;
			broker4->sin_family = AF_INET;
			broker4->sin_port = htons(CONFIG_MQTT_BROKER_PORT);

			inet_ntop(AF_INET, &broker4->sin_addr.s_addr,
					  ipv4_addr, sizeof(ipv4_addr));
			LOG_INF("IPv4 Address found %s\n", (char *)(ipv4_addr));

			break;
		}
		else
		{
			LOG_WRN("ai_addrlen = %u should be %u or %u\n",
					(unsigned int)addr->ai_addrlen,
					(unsigned int)sizeof(struct sockaddr_in),
					(unsigned int)sizeof(struct sockaddr_in6));
		}

		addr = addr->ai_next;
	}

	/* Free the address. */
	freeaddrinfo(result);

	return err;
}

/* Function to get the client id */
static const uint8_t *client_id_get(void)
{
	static uint8_t client_id[MAX(sizeof(CONFIG_MQTT_CLIENT_ID),
								 CLIENT_ID_LEN)];

	if (strlen(CONFIG_MQTT_CLIENT_ID) > 0)
	{
		snprintf(client_id, sizeof(client_id), "%s",
				 CONFIG_MQTT_CLIENT_ID);
		goto exit;
	}

	char imei_buf[CGSN_RESPONSE_LENGTH + 1];
	int err;

	err = nrf_modem_at_cmd(imei_buf, sizeof(imei_buf), "AT+CGSN");
	if (err)
	{
		LOG_WRN("Failed to obtain IMEI, error: %d\n", err);
		goto exit;
	}

	imei_buf[IMEI_LEN] = '\0';

	snprintf(client_id, sizeof(client_id), "nrf-%.*s", IMEI_LEN, imei_buf);

exit:
	LOG_DBG("client_id = %s", (char *)(client_id));

	return client_id;
}

	/**@brief Initialize the MQTT client structure
	 */
	/* STEP 3 - Define the function client_init() to initialize the MQTT client instance.  */
	int	client_init()
{
	int err;

	/* initializes the client instance. */
	mqtt_client_init(&client);
	/* Resolves the configured hostname and initializes the MQTT broker structure */
	err = broker_init();
	if (err)
	{
		LOG_WRN("Failed to initialize broker connection\n");
		return err;
	}
	/* MQTT client configuration */
	client.broker = &broker;
	client.evt_cb = mqtt_evt_handler;
	client.client_id.utf8 = client_id_get();
	client.client_id.size = strlen(client.client_id.utf8);
	client.password = NULL;
	client.user_name = NULL;
	client.protocol_version = MQTT_VERSION_3_1_1;
	/* MQTT buffers configuration */
	client.rx_buf = rx_buffer;
	client.rx_buf_size = sizeof(rx_buffer);
	client.tx_buf = tx_buffer;
	client.tx_buf_size = sizeof(tx_buffer);
	/* We are not using TLS in Exercise 1 */
	client.transport.type = MQTT_TRANSPORT_NON_SECURE;
	return err;
}

void mqtt_idleloop()
{
	int err;
	uint32_t connect_attempt = 0;

do_connect:
	if (connect_attempt++ > 0)
	{
		LOG_INF("Reconnecting in %d seconds...\n",
				CONFIG_MQTT_RECONNECT_DELAY_S);
		k_sleep(K_SECONDS(CONFIG_MQTT_RECONNECT_DELAY_S));
	}
	err = mqtt_connect(&client);
	if (err)
	{
		LOG_WRN("Error in mqtt_connect: %d\n", err);
		goto do_connect;
	}

	err = fds_init(&client, &fds);
	if (err)
	{
		LOG_WRN("Error in fds_init: %d\n", err);
		return;
	}

	while (1)
	{
		err = poll(&fds, 1, mqtt_keepalive_time_left(&client));
		if (err < 0)
		{
			LOG_WRN("Error in poll(): %d\n", errno);
			break;
		}

		err = mqtt_live(&client);
		if ((err != 0) && (err != -EAGAIN))
		{
			LOG_WRN("Error in mqtt_live: %d\n", err);
			break;
		}

		if ((fds.revents & POLLIN) == POLLIN)
		{
			err = mqtt_input(&client);
			if (err != 0)
			{
				LOG_WRN("Error in mqtt_input: %d\n", err);
				break;
			}
		}

		if ((fds.revents & POLLERR) == POLLERR)
		{
			LOG_WRN("POLLERR\n");
			break;
		}

		if ((fds.revents & POLLNVAL) == POLLNVAL)
		{
			LOG_WRN("POLLNVAL\n");
			break;
		}
	}

	LOG_INF("Disconnecting MQTT client\n");

	err = mqtt_disconnect(&client);
	if (err)
	{
		LOG_WRN("Could not disconnect MQTT client: %d\n", err);
	}
	goto do_connect;
}

/**@brief Initialize the file descriptor structure used by poll.
 */
int fds_init(struct mqtt_client *c, struct pollfd *fds)
{
	if (c->transport.type == MQTT_TRANSPORT_NON_SECURE)
	{
		fds->fd = c->transport.tcp.sock;
	}
	else
	{
		return -ENOTSUP;
	}

	fds->events = POLLIN;

	return 0;
}