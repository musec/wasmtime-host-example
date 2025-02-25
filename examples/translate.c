int
translate(char *data_buffer, int buffer_len)
{
	// First, just for fun, let's calculate strlen().
	int len = 0;
	while (data_buffer[len])
	{
		len += 1;
	}

	// Now, let's modify the bytes after the string.
	for (int i = len + 1; i < buffer_len; i++)
	{
		data_buffer[i] += 1;
	}

	return len;
}
