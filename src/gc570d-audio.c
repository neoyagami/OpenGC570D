// SPDX-License-Identifier: GPL-2.0-only
/*
 * ALSA PCM capture integration and audio DMA handling for both HDMI inputs.
 */
#include "gc570d.h"

static int gc570d_audio_take_period(void *buffer_cpu[2], void *destination)
{
	u32 descriptor;
	u32 best_slot = 0;
	u32 slot;
	size_t best_changed = 0;
	size_t changed;
	size_t offset;
	size_t i;

	dma_rmb();
	for (slot = 0;
	     slot < 2 * (GC570D_AUDIO_STAGING_SIZE /
			 GC570D_AUDIO_PERIOD_BYTES);
	     slot++) {
		descriptor = slot /
			(GC570D_AUDIO_STAGING_SIZE / GC570D_AUDIO_PERIOD_BYTES);
		offset = (slot %
			 (GC570D_AUDIO_STAGING_SIZE /
			  GC570D_AUDIO_PERIOD_BYTES)) *
			 GC570D_AUDIO_PERIOD_BYTES;
		changed = 0;
		for (i = 0; i < GC570D_AUDIO_PERIOD_BYTES; i++)
			changed += ((u8 *)buffer_cpu[descriptor])[offset + i] !=
				   GC570D_AUDIO_SENTINEL;
		if (changed > best_changed) {
			best_changed = changed;
			best_slot = slot;
		}
	}
	if (best_changed < GC570D_AUDIO_PERIOD_BYTES / 2)
		return -ENODATA;

	descriptor = best_slot /
		(GC570D_AUDIO_STAGING_SIZE / GC570D_AUDIO_PERIOD_BYTES);
	offset = (best_slot %
		  (GC570D_AUDIO_STAGING_SIZE / GC570D_AUDIO_PERIOD_BYTES)) *
		 GC570D_AUDIO_PERIOD_BYTES;
	memcpy(destination, buffer_cpu[descriptor] + offset,
	       GC570D_AUDIO_PERIOD_BYTES);
	memset(buffer_cpu[descriptor] + offset, GC570D_AUDIO_SENTINEL,
	       GC570D_AUDIO_PERIOD_BYTES);
	dma_wmb();

	return 0;
}

static int gc570d_audio0_dma_capture_periods(struct gc570d_dev *gc,
					      unsigned int periods)
{
	void *buffer_cpu[2] = { NULL, NULL };
	dma_addr_t buffer_dma[2] = { 0, 0 };
	ktime_t capture_start;
	u64 term_baseline;
	u32 irq_status = 0;
	u32 start_control;
	u32 index;
	long waited;
	size_t changed = 0;
	size_t i;
	int ret;

	mutex_lock(&gc->capture_lock);
	if (!READ_ONCE(gc->video0_streaming)) {
		ret = -EAGAIN;
		goto out_status;
	}
	if (readl(gc->bar0 + GC570D_AUDIO_START) &
	    GC570D_AUDIO0_START_BIT) {
		ret = -EBUSY;
		goto out_status;
	}

	vfree(gc->audio0_capture_data);
	gc->audio0_capture_data = NULL;
	gc->audio0_capture_size = 0;
	gc->audio0_capture_changed = 0;
	gc->audio0_capture_irq = 0;
	gc->audio0_capture_index = 0;
	gc->audio0_capture_control = 0;
	gc->audio0_capture_requested = periods;
	gc->audio0_capture_completed = 0;
	gc->audio0_capture_elapsed_us = 0;
	gc->audio0_capture_error = 0;
	gc->audio0_capture_data = vmalloc(array_size(periods,
						     GC570D_AUDIO_PERIOD_BYTES));
	if (!gc->audio0_capture_data) {
		ret = -ENOMEM;
		goto out_status;
	}

	ret = gc570d_it68051_audio_output_set(gc, true);
	if (ret)
		goto out_status;
	for (i = 0; i < 2; i++) {
		buffer_cpu[i] = dma_alloc_coherent(&gc->pdev->dev,
						   GC570D_AUDIO_BUFFER_SIZE,
						   &buffer_dma[i], GFP_KERNEL);
		if (!buffer_cpu[i]) {
			ret = -ENOMEM;
			goto out_free;
		}
		memset(buffer_cpu[i], GC570D_AUDIO_SENTINEL,
		       GC570D_AUDIO_BUFFER_SIZE);
	}

	/* Use the official driver's channel-0 parameters: S16LE, 2ch, 48 kHz. */
	gc570d_reset_audio_channel(gc, GC570D_AUDIO0_CHANNEL);
	writel(0, gc->bar0 + GC570D_AUDIO0_FORMAT);
	writel(0, gc->bar0 + GC570D_AUDIO_GLOBAL_FORMAT);
	writel(GC570D_AUDIO_PERIOD_FRAMES, gc->bar0 + GC570D_AUDIO0_RATE);
	writel(lower_32_bits(buffer_dma[0]),
	       gc->bar0 + GC570D_AUDIO0_BUFFER0);
	writel(upper_32_bits(buffer_dma[0]),
	       gc->bar0 + GC570D_AUDIO0_BUFFER0 + 4);
	writel(lower_32_bits(buffer_dma[1]),
	       gc->bar0 + GC570D_AUDIO0_BUFFER1);
	writel(upper_32_bits(buffer_dma[1]),
	       gc->bar0 + GC570D_AUDIO0_BUFFER1 + 4);
	dma_wmb();

	atomic_set(&gc->audio0_irq_pending, 0);
	writel(GC570D_AUDIO0_IRQ | GC570D_AUDIO0_TERM_IRQ,
	       gc->bar0 + GC570D_REG_IRQ_STATUS);
	pci_set_master(gc->pdev);
	term_baseline = atomic64_read(&gc->irq_audio0_term);
	start_control = readl(gc->bar0 + GC570D_AUDIO_START);
	capture_start = ktime_get();
	writel(start_control | GC570D_AUDIO0_START_BIT,
	       gc->bar0 + GC570D_AUDIO_START);
	ret = 0;
	while (gc->audio0_capture_completed < periods) {
		waited = wait_event_interruptible_timeout(
			gc->audio0_wait,
			atomic_read(&gc->audio0_irq_pending) ||
			!READ_ONCE(gc->video0_streaming),
			msecs_to_jiffies(500));
		if (waited < 0) {
			ret = waited;
			break;
		}
		if (!waited) {
			ret = -ETIMEDOUT;
			break;
		}
		if (!READ_ONCE(gc->video0_streaming)) {
			ret = -EPIPE;
			break;
		}
		atomic_set(&gc->audio0_irq_pending, 0);
		irq_status = readl(gc->bar0 + GC570D_REG_IRQ_STATUS);
		index = readl(gc->bar0 + 0x14) & 0x03;
		gc->audio0_capture_irq = irq_status | GC570D_AUDIO0_IRQ;
		gc->audio0_capture_index = index;
		if (index < 1 || index > 2) {
			ret = -EIO;
			break;
		}
		ret = gc570d_audio_take_period(
			buffer_cpu,
			gc->audio0_capture_data +
			gc->audio0_capture_completed * GC570D_AUDIO_PERIOD_BYTES);
		/* A level INTx may repeat before a new staging slot is visible. */
		if (ret == -ENODATA) {
			ret = 0;
			continue;
		}
		if (ret)
			break;
		gc->audio0_capture_completed++;
	}
	gc->audio0_capture_elapsed_us =
		ktime_us_delta(ktime_get(), capture_start);

	start_control = readl(gc->bar0 + GC570D_AUDIO_START);
	writel(start_control & ~GC570D_AUDIO0_START_BIT,
	       gc->bar0 + GC570D_AUDIO_START);
	wake_up_interruptible(&gc->audio0_wait);
	readl_poll_timeout(gc->bar0 + GC570D_REG_IRQ_STATUS, irq_status,
			   (irq_status & GC570D_AUDIO0_TERM_IRQ) ||
			   atomic64_read(&gc->irq_audio0_term) != term_baseline,
			   100, 100000);
	writel(GC570D_AUDIO0_IRQ | GC570D_AUDIO0_TERM_IRQ,
	       gc->bar0 + GC570D_REG_IRQ_STATUS);
	gc570d_reset_audio_channel(gc, GC570D_AUDIO0_CHANNEL);
	gc->audio0_capture_control = readl(gc->bar0 + GC570D_AUDIO_START);

	gc->audio0_capture_size = gc->audio0_capture_completed *
				  GC570D_AUDIO_PERIOD_BYTES;
	for (i = 0; i < gc->audio0_capture_size; i++)
		changed += gc->audio0_capture_data[i] != GC570D_AUDIO_SENTINEL;
	gc->audio0_capture_changed = changed;
	if (!ret && !changed)
		ret = -ENODATA;

out_free:
	for (i = 0; i < 2; i++) {
		if (buffer_cpu[i])
			dma_free_coherent(&gc->pdev->dev,
					  GC570D_AUDIO_BUFFER_SIZE,
					  buffer_cpu[i], buffer_dma[i]);
	}
out_status:
	gc->audio0_capture_error = ret;
	dev_info(&gc->pdev->dev,
		 "HDMI IN 1 audio DMA0 periods=%u/%u result=%d irq=0x%08x index=%u changed=%zu/%zu elapsed_us=%llu\n",
		 gc->audio0_capture_completed, gc->audio0_capture_requested,
		 ret, gc->audio0_capture_irq, gc->audio0_capture_index,
		 gc->audio0_capture_changed, gc->audio0_capture_size,
		 gc->audio0_capture_elapsed_us);
	mutex_unlock(&gc->capture_lock);
	return ret;
}

static ssize_t gc570d_audio0_dma_record_write(struct file *file,
					       const char __user *buffer,
					       size_t count, loff_t *position)
{
	struct gc570d_dev *gc = file->private_data;
	bool requested;
	int ret;

	ret = kstrtobool_from_user(buffer, count, &requested);
	if (ret)
		return ret;
	if (!requested)
		return -EINVAL;
	ret = gc570d_audio0_dma_capture_periods(gc,
						GC570D_AUDIO_RECORD_PERIODS);
	return ret ? ret : count;
}

const struct file_operations gc570d_audio0_dma_record_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = gc570d_audio0_dma_record_write,
	.llseek = noop_llseek,
};

static ssize_t gc570d_audio0_dma_frame_read(struct file *file,
					     char __user *buffer,
					     size_t count, loff_t *position)
{
	struct gc570d_dev *gc = file->private_data;
	ssize_t ret;

	mutex_lock(&gc->capture_lock);
	ret = simple_read_from_buffer(buffer, count, position,
				      gc->audio0_capture_data,
				      gc->audio0_capture_size);
	mutex_unlock(&gc->capture_lock);
	return ret;
}

const struct file_operations gc570d_audio0_dma_frame_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = gc570d_audio0_dma_frame_read,
	.llseek = default_llseek,
};

static int gc570d_audio0_dma_status_show(struct seq_file *s, void *unused)
{
	struct gc570d_dev *gc = s->private;

	mutex_lock(&gc->capture_lock);
	seq_printf(s,
		   "error=%d irq=0x%08x index=%u control=0x%08x periods=%u/%u changed=%zu size=%zu period_bytes=%u elapsed_us=%llu\n",
		   gc->audio0_capture_error, gc->audio0_capture_irq,
		   gc->audio0_capture_index, gc->audio0_capture_control,
		   gc->audio0_capture_completed, gc->audio0_capture_requested,
		   gc->audio0_capture_changed, gc->audio0_capture_size,
		   GC570D_AUDIO_PERIOD_BYTES, gc->audio0_capture_elapsed_us);
	mutex_unlock(&gc->capture_lock);
	return 0;
}
GC570D_DEFINE_SHOW_ATTRIBUTE(gc570d_audio0_dma_status);

static int gc570d_audio_dma_capture_periods(struct gc570d_dev *gc,
					     unsigned int periods)
{
	const struct gc570d_i2c_bus *bus = &gc570d_receiver_buses[1];
	void *buffer_cpu[2] = { NULL, NULL };
	dma_addr_t buffer_dma[2] = { 0, 0 };
	ktime_t capture_start;
	u32 irq_status = 0;
	u32 start_control;
	u32 index;
	size_t changed = 0;
	size_t i;
	int mute_ret;
	int ret;

	mutex_lock(&gc->capture_lock);
	if (gc->video_streaming || gc->video0_streaming) {
		ret = -EBUSY;
		goto out_status;
	}
	if (readl(gc->bar0 + GC570D_AUDIO_START) & GC570D_AUDIO_START_BIT) {
		ret = -EBUSY;
		goto out_status;
	}

	vfree(gc->audio_capture_data);
	gc->audio_capture_data = NULL;
	gc->audio_capture_size = 0;
	gc->audio_capture_changed = 0;
	gc->audio_capture_irq = 0;
	gc->audio_capture_index = 0;
	gc->audio_capture_control = 0;
	gc->audio_capture_requested = periods;
	gc->audio_capture_completed = 0;
	gc->audio_capture_elapsed_us = 0;
	gc->audio_capture_mute_error = 0;
	gc->audio_capture_error = 0;
	gc->audio_capture_data = vmalloc(array_size(periods,
						    GC570D_AUDIO_PERIOD_BYTES));
	if (!gc->audio_capture_data) {
		ret = -ENOMEM;
		goto out_status;
	}

	buffer_cpu[0] = dma_alloc_coherent(&gc->pdev->dev,
					  GC570D_AUDIO_BUFFER_SIZE,
					  &buffer_dma[0], GFP_KERNEL);
	if (!buffer_cpu[0]) {
		ret = -ENOMEM;
		goto out_status;
	}
	buffer_cpu[1] = dma_alloc_coherent(&gc->pdev->dev,
					  GC570D_AUDIO_BUFFER_SIZE,
					  &buffer_dma[1], GFP_KERNEL);
	if (!buffer_cpu[1]) {
		ret = -ENOMEM;
		goto out_free;
	}
	memset(buffer_cpu[0], GC570D_AUDIO_SENTINEL,
	       GC570D_AUDIO_BUFFER_SIZE);
	memset(buffer_cpu[1], GC570D_AUDIO_SENTINEL,
	       GC570D_AUDIO_BUFFER_SIZE);

	gc570d_reset_audio_channel(gc, GC570D_AUDIO_CHANNEL);
	writel(0, gc->bar0 + GC570D_AUDIO_FORMAT);
	writel(0, gc->bar0 + GC570D_AUDIO_GLOBAL_FORMAT);
	writel(480, gc->bar0 + GC570D_AUDIO_RATE);
	writel(lower_32_bits(buffer_dma[0]),
	       gc->bar0 + GC570D_AUDIO_BUFFER0);
	writel(upper_32_bits(buffer_dma[0]),
	       gc->bar0 + GC570D_AUDIO_BUFFER0 + 4);
	writel(lower_32_bits(buffer_dma[1]),
	       gc->bar0 + GC570D_AUDIO_BUFFER1);
	writel(upper_32_bits(buffer_dma[1]),
	       gc->bar0 + GC570D_AUDIO_BUFFER1 + 4);
	dma_wmb();

	writel(GC570D_AUDIO_IRQ, gc->bar0 + GC570D_REG_IRQ_STATUS);
	pci_set_master(gc->pdev);
	start_control = readl(gc->bar0 + GC570D_AUDIO_START);
	capture_start = ktime_get();
	writel(start_control | GC570D_AUDIO_START_BIT,
	       gc->bar0 + GC570D_AUDIO_START);
	ret = 0;
	while (gc->audio_capture_completed < periods) {
		ret = readl_poll_timeout(gc->bar0 + GC570D_REG_IRQ_STATUS,
					 irq_status,
					 irq_status & GC570D_AUDIO_IRQ,
					 100, 500000);
		if (ret)
			break;

		index = (readl(gc->bar0 + 0x14) >>
			 (GC570D_AUDIO_CHANNEL * 2)) & 0x03;
		gc->audio_capture_irq = irq_status;
		gc->audio_capture_index = index;
		if (index < 1 || index > 2) {
			ret = -EIO;
			break;
		}

		ret = gc570d_audio_take_period(
			buffer_cpu,
			gc->audio_capture_data +
			gc->audio_capture_completed * GC570D_AUDIO_PERIOD_BYTES);
		if (ret)
			break;
		gc->audio_capture_completed++;
		writel(GC570D_AUDIO_IRQ,
		       gc->bar0 + GC570D_REG_IRQ_STATUS);
	}
	gc->audio_capture_elapsed_us =
		ktime_us_delta(ktime_get(), capture_start);

	/* Mute the source side while the bridge audio path still responds. */
	mute_ret = gc570d_receiver_update8(gc, bus, 0x52, 0x1f, 0x1f);
	gc->audio_capture_mute_error = mute_ret;
	start_control = readl(gc->bar0 + GC570D_AUDIO_START);
	writel(start_control & ~GC570D_AUDIO_START_BIT,
	       gc->bar0 + GC570D_AUDIO_START);
	writel(GC570D_AUDIO_IRQ, gc->bar0 + GC570D_REG_IRQ_STATUS);
	gc570d_reset_audio_channel(gc, GC570D_AUDIO_CHANNEL);
	pci_clear_master(gc->pdev);
	gc->audio_capture_control = readl(gc->bar0 + GC570D_AUDIO_START);

	gc->audio_capture_size = gc->audio_capture_completed *
				 GC570D_AUDIO_PERIOD_BYTES;
	for (i = 0; i < gc->audio_capture_size; i++)
		changed += gc->audio_capture_data[i] != GC570D_AUDIO_SENTINEL;
	gc->audio_capture_changed = changed;
	if (!ret && !changed)
		ret = -ENODATA;

out_free:
	if (buffer_cpu[1])
		dma_free_coherent(&gc->pdev->dev, GC570D_AUDIO_BUFFER_SIZE,
				  buffer_cpu[1], buffer_dma[1]);
	if (buffer_cpu[0])
		dma_free_coherent(&gc->pdev->dev, GC570D_AUDIO_BUFFER_SIZE,
				  buffer_cpu[0], buffer_dma[0]);
out_status:
	gc->audio_capture_error = ret;
	dev_info(&gc->pdev->dev,
		 "audio DMA capture periods=%u/%u result=%d irq=0x%08x index=%u changed=%zu/%zu elapsed_us=%llu\n",
		 gc->audio_capture_completed, gc->audio_capture_requested,
		 ret, gc->audio_capture_irq, gc->audio_capture_index,
		 gc->audio_capture_changed, gc->audio_capture_size,
		 gc->audio_capture_elapsed_us);
	mutex_unlock(&gc->capture_lock);
	return ret;
}

static ssize_t gc570d_audio_dma_capture_write(struct file *file,
					       const char __user *buffer,
					       size_t count, loff_t *position)
{
	struct gc570d_dev *gc = file->private_data;
	bool requested;
	int ret;

	ret = kstrtobool_from_user(buffer, count, &requested);
	if (ret)
		return ret;
	if (!requested)
		return -EINVAL;
	ret = gc570d_audio_dma_capture_periods(gc, 1);
	return ret ? ret : count;
}

const struct file_operations gc570d_audio_dma_capture_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = gc570d_audio_dma_capture_write,
	.llseek = noop_llseek,
};

static ssize_t gc570d_audio_dma_record_write(struct file *file,
					      const char __user *buffer,
					      size_t count, loff_t *position)
{
	struct gc570d_dev *gc = file->private_data;
	bool requested;
	int ret;

	ret = kstrtobool_from_user(buffer, count, &requested);
	if (ret)
		return ret;
	if (!requested)
		return -EINVAL;
	ret = gc570d_audio_dma_capture_periods(gc,
					       GC570D_AUDIO_RECORD_PERIODS);
	return ret ? ret : count;
}

const struct file_operations gc570d_audio_dma_record_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = gc570d_audio_dma_record_write,
	.llseek = noop_llseek,
};

static ssize_t gc570d_audio_dma_frame_read(struct file *file,
					    char __user *buffer,
					    size_t count, loff_t *position)
{
	struct gc570d_dev *gc = file->private_data;
	ssize_t ret;

	mutex_lock(&gc->capture_lock);
	ret = simple_read_from_buffer(buffer, count, position,
				      gc->audio_capture_data,
				      gc->audio_capture_size);
	mutex_unlock(&gc->capture_lock);
	return ret;
}

const struct file_operations gc570d_audio_dma_frame_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = gc570d_audio_dma_frame_read,
	.llseek = default_llseek,
};

static ssize_t gc570d_audio_dma_period_read(struct file *file,
					     char __user *buffer,
					     size_t count, loff_t *position)
{
	struct gc570d_dev *gc = file->private_data;
	ssize_t ret;

	mutex_lock(&gc->capture_lock);
	if (!gc->audio_capture_data ||
	    gc->audio_capture_size < GC570D_AUDIO_PERIOD_BYTES) {
		ret = -ENODATA;
		goto out_unlock;
	}
	ret = simple_read_from_buffer(buffer, count, position,
				      gc->audio_capture_data,
				      GC570D_AUDIO_PERIOD_BYTES);

out_unlock:
	mutex_unlock(&gc->capture_lock);
	return ret;
}

const struct file_operations gc570d_audio_dma_period_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = gc570d_audio_dma_period_read,
	.llseek = default_llseek,
};

static int gc570d_audio_dma_status_show(struct seq_file *s, void *unused)
{
	struct gc570d_dev *gc = s->private;

	mutex_lock(&gc->capture_lock);
	seq_printf(s,
		   "error=%d mute_error=%d irq=0x%08x index=%u control=0x%08x periods=%u/%u changed=%zu size=%zu period_bytes=%u elapsed_us=%llu\n",
		   gc->audio_capture_error, gc->audio_capture_mute_error,
		   gc->audio_capture_irq, gc->audio_capture_index,
		   gc->audio_capture_control, gc->audio_capture_completed,
		   gc->audio_capture_requested,
		   gc->audio_capture_changed, gc->audio_capture_size,
		   GC570D_AUDIO_PERIOD_BYTES, gc->audio_capture_elapsed_us);
	mutex_unlock(&gc->capture_lock);
	return 0;
}
GC570D_DEFINE_SHOW_ATTRIBUTE(gc570d_audio_dma_status);

static const struct snd_pcm_hardware gc570d_audio_pcm_hardware = {
	.info = SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rates = SNDRV_PCM_RATE_48000,
	.rate_min = GC570D_AUDIO_RATE_HZ,
	.rate_max = GC570D_AUDIO_RATE_HZ,
	.channels_min = GC570D_AUDIO_CHANNELS,
	.channels_max = GC570D_AUDIO_CHANNELS,
	.buffer_bytes_max = GC570D_AUDIO_PCM_BUFFER_MAX,
	.period_bytes_min = GC570D_AUDIO_PERIOD_BYTES,
	.period_bytes_max = GC570D_AUDIO_PERIOD_BYTES,
	.periods_min = 2,
	.periods_max = GC570D_AUDIO_PCM_BUFFER_MAX /
		       GC570D_AUDIO_PERIOD_BYTES,
};

static int gc570d_audio_pcm_thread(void *data)
{
	struct gc570d_dev *gc = data;
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	u8 period[GC570D_AUDIO_PERIOD_BYTES];
	u32 irq_status;
	u32 index;
	size_t buffer_bytes;
	size_t first;
	size_t position;
	int ret;

	while (!kthread_should_stop()) {
		wait_event_interruptible(gc->audio_wait,
			kthread_should_stop() || READ_ONCE(gc->audio_running));
		if (kthread_should_stop())
			break;

		ret = readl_poll_timeout(gc->bar0 + GC570D_REG_IRQ_STATUS,
					 irq_status,
					 (irq_status & GC570D_AUDIO_IRQ) ||
					 atomic_read(&gc->audio_irq_pending),
					 100, 500000);
		if (!READ_ONCE(gc->audio_running))
			continue;
		if (ret)
			goto xrun;
		atomic_set(&gc->audio_irq_pending, 0);

		index = (readl(gc->bar0 + 0x14) >>
			 (GC570D_AUDIO_CHANNEL * 2)) & 0x03;
		if (index < 1 || index > 2) {
			ret = -EIO;
			goto xrun;
		}
		ret = gc570d_audio_take_period(gc->audio_pcm_buffer_cpu,
					       period);
		writel(GC570D_AUDIO_IRQ, gc->bar0 + GC570D_REG_IRQ_STATUS);
		/* INTx can repeat before a new staging period becomes visible. */
		if (ret == -ENODATA)
			continue;
		if (ret)
			goto xrun;

		substream = READ_ONCE(gc->audio_substream);
		if (!substream || !substream->runtime) {
			ret = -ENODEV;
			goto xrun;
		}
		runtime = substream->runtime;
		buffer_bytes = frames_to_bytes(runtime, runtime->buffer_size);
		position = READ_ONCE(gc->audio_pcm_pos);
		first = min_t(size_t, GC570D_AUDIO_PERIOD_BYTES,
			      buffer_bytes - position);
		memcpy(runtime->dma_area + position, period, first);
		if (first < GC570D_AUDIO_PERIOD_BYTES)
			memcpy(runtime->dma_area, period + first,
			       GC570D_AUDIO_PERIOD_BYTES - first);
		position = (position + GC570D_AUDIO_PERIOD_BYTES) % buffer_bytes;
		WRITE_ONCE(gc->audio_pcm_pos, position);
		snd_pcm_period_elapsed(substream);
		continue;

xrun:
		gc->audio_thread_error = ret;
		WRITE_ONCE(gc->audio_running, false);
		atomic_set(&gc->audio_irq_pending, 0);
		writel(readl(gc->bar0 + GC570D_AUDIO_START) &
		       ~GC570D_AUDIO_START_BIT,
		       gc->bar0 + GC570D_AUDIO_START);
		writel(GC570D_AUDIO_IRQ, gc->bar0 + GC570D_REG_IRQ_STATUS);
		substream = READ_ONCE(gc->audio_substream);
		if (substream)
			snd_pcm_stop_xrun(substream);
	}

	return 0;
}

static int gc570d_audio_pcm_open(struct snd_pcm_substream *substream)
{
	int ret;

	substream->runtime->hw = gc570d_audio_pcm_hardware;
	/* Allow a transient DMA pause without failing a blocking reader. */
	substream->wait_time = GC570D_AUDIO_WAIT_TIME_MS;
	ret = snd_pcm_hw_constraint_integer(substream->runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
	return ret;
}

static int gc570d_audio_pcm_close(struct snd_pcm_substream *substream)
{
	return 0;
}

static int gc570d_audio_pcm_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params)
{
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
}

static void gc570d_audio_finish_stop(struct gc570d_dev *gc)
{
	u32 status = readl(gc->bar0 + GC570D_REG_IRQ_STATUS);
	int ret = 0;

	if (gc->audio_stop_pending) {
		ret = readl_poll_timeout(gc->bar0 + GC570D_REG_IRQ_STATUS,
					 status,
					 (status & GC570D_AUDIO_TERM_IRQ) ||
					 atomic64_read(&gc->irq_audio_term) !=
					 gc->audio_term_baseline,
					 100, 100000);
		if (status & GC570D_AUDIO_TERM_IRQ)
			atomic64_inc(&gc->irq_audio_term);
		gc->audio_stop_pending = false;
	}

	/* These events are W1C and must not survive into the next stream. */
	writel(GC570D_AUDIO_IRQ | GC570D_AUDIO_TERM_IRQ,
	       gc->bar0 + GC570D_REG_IRQ_STATUS);
	if (ret)
		dev_warn(&gc->pdev->dev,
			 "audio DMA termination was not observed before release\n");
}

static int gc570d_audio_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct gc570d_dev *gc = snd_pcm_substream_chip(substream);
	struct task_struct *thread;
	int i;

	WRITE_ONCE(gc->audio_running, false);
	atomic_set(&gc->audio_irq_pending, 0);
	writel(readl(gc->bar0 + GC570D_AUDIO_START) &
	       ~GC570D_AUDIO_START_BIT, gc->bar0 + GC570D_AUDIO_START);
	writel(GC570D_AUDIO_IRQ, gc->bar0 + GC570D_REG_IRQ_STATUS);
	wake_up_interruptible(&gc->audio_wait);
	thread = gc->audio_thread;
	gc->audio_thread = NULL;
	if (thread)
		kthread_stop(thread);

	mutex_lock(&gc->capture_lock);
	gc570d_audio_finish_stop(gc);
	for (i = 0; i < 2; i++) {
		if (gc->audio_pcm_buffer_cpu[i])
			dma_free_coherent(&gc->pdev->dev,
					  GC570D_AUDIO_BUFFER_SIZE,
					  gc->audio_pcm_buffer_cpu[i],
					  gc->audio_pcm_buffer_dma[i]);
		gc->audio_pcm_buffer_cpu[i] = NULL;
	}
	if (gc->audio_irq_active) {
		gc570d_video_irq_put_locked(gc);
		gc->audio_irq_active = false;
	}
	if (gc->audio_substream == substream)
		gc->audio_substream = NULL;
	gc->audio_prepared = false;
	if (!gc->video_streaming && !gc->video0_streaming &&
	    !gc->audio0_prepared)
		pci_clear_master(gc->pdev);
	mutex_unlock(&gc->capture_lock);

	return snd_pcm_lib_free_pages(substream);
}

static int gc570d_audio_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct gc570d_dev *gc = snd_pcm_substream_chip(substream);
	int i;
	int ret;

	mutex_lock(&gc->capture_lock);
	if (READ_ONCE(gc->suspended)) {
		ret = -EBUSY;
		goto out_unlock;
	}
	if (gc->audio_prepared) {
		ret = 0;
		gc->audio_pcm_pos = 0;
		goto out_unlock;
	}

	/*
	 * ALSA and V4L2 are independent pins.  PipeWire may prepare this PCM
	 * before OBS starts HDMI IN 2 video, so establish the receiver state and
	 * take an independent reference to the shared bridge IRQ.
	 */
	if (!gc->video_streaming) {
		ret = gc570d_it6802_output_enable(gc);
		if (ret)
			goto out_unlock;
		ret = gc570d_it6802_format_init(gc);
		if (ret)
			goto out_unlock;
	}
	ret = gc570d_it6802_audio_output_set(gc, true);
	if (ret)
		goto out_unlock;
	for (i = 0; i < 2; i++) {
		gc->audio_pcm_buffer_cpu[i] =
			dma_alloc_coherent(&gc->pdev->dev,
					   GC570D_AUDIO_BUFFER_SIZE,
					   &gc->audio_pcm_buffer_dma[i],
					   GFP_KERNEL);
		if (!gc->audio_pcm_buffer_cpu[i]) {
			ret = -ENOMEM;
			goto out_free;
		}
		memset(gc->audio_pcm_buffer_cpu[i], GC570D_AUDIO_SENTINEL,
		       GC570D_AUDIO_BUFFER_SIZE);
	}

	gc570d_reset_audio_channel(gc, GC570D_AUDIO_CHANNEL);
	writel(GC570D_AUDIO_IRQ | GC570D_AUDIO_TERM_IRQ,
	       gc->bar0 + GC570D_REG_IRQ_STATUS);
	writel(0, gc->bar0 + GC570D_AUDIO_FORMAT);
	writel(0, gc->bar0 + GC570D_AUDIO_GLOBAL_FORMAT);
	writel(480, gc->bar0 + GC570D_AUDIO_RATE);
	writel(lower_32_bits(gc->audio_pcm_buffer_dma[0]),
	       gc->bar0 + GC570D_AUDIO_BUFFER0);
	writel(upper_32_bits(gc->audio_pcm_buffer_dma[0]),
	       gc->bar0 + GC570D_AUDIO_BUFFER0 + 4);
	writel(lower_32_bits(gc->audio_pcm_buffer_dma[1]),
	       gc->bar0 + GC570D_AUDIO_BUFFER1);
	writel(upper_32_bits(gc->audio_pcm_buffer_dma[1]),
	       gc->bar0 + GC570D_AUDIO_BUFFER1 + 4);
	dma_wmb();

	gc->audio_pcm_pos = 0;
	gc->audio_thread_error = 0;
	atomic_set(&gc->audio_irq_pending, 0);
	gc->audio_substream = substream;
	gc->audio_prepared = true;
	pci_set_master(gc->pdev);
	gc570d_video_irq_get_locked(gc);
	gc->audio_irq_active = true;
	gc->audio_thread = kthread_run(gc570d_audio_pcm_thread, gc,
				       "gc570d-audio1");
	if (IS_ERR(gc->audio_thread)) {
		ret = PTR_ERR(gc->audio_thread);
		gc->audio_thread = NULL;
		gc->audio_substream = NULL;
		gc->audio_prepared = false;
		if (gc->audio_irq_active) {
			gc570d_video_irq_put_locked(gc);
			gc->audio_irq_active = false;
		}
		if (!gc->video_streaming && !gc->video0_streaming &&
		    !gc->audio0_prepared)
			pci_clear_master(gc->pdev);
		goto out_free;
	}
	mutex_unlock(&gc->capture_lock);
	return 0;

out_free:
	for (i = 0; i < 2; i++) {
		if (gc->audio_pcm_buffer_cpu[i])
			dma_free_coherent(&gc->pdev->dev,
					  GC570D_AUDIO_BUFFER_SIZE,
					  gc->audio_pcm_buffer_cpu[i],
					  gc->audio_pcm_buffer_dma[i]);
		gc->audio_pcm_buffer_cpu[i] = NULL;
	}
out_unlock:
	mutex_unlock(&gc->capture_lock);
	return ret;
}

static int gc570d_audio_pcm_trigger(struct snd_pcm_substream *substream,
				    int command)
{
	struct gc570d_dev *gc = snd_pcm_substream_chip(substream);
	u32 control;

	switch (command) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (!READ_ONCE(gc->audio_prepared))
			return -EIO;
		atomic_set(&gc->audio_irq_pending, 0);
		writel(GC570D_AUDIO_IRQ | GC570D_AUDIO_TERM_IRQ,
		       gc->bar0 + GC570D_REG_IRQ_STATUS);
		gc->audio_term_baseline = atomic64_read(&gc->irq_audio_term);
		gc->audio_stop_pending = true;
		WRITE_ONCE(gc->audio_running, true);
		control = readl(gc->bar0 + GC570D_AUDIO_START);
		writel(control | GC570D_AUDIO_START_BIT,
		       gc->bar0 + GC570D_AUDIO_START);
		wake_up_interruptible(&gc->audio_wait);
		return 0;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		WRITE_ONCE(gc->audio_running, false);
		atomic_set(&gc->audio_irq_pending, 0);
		control = readl(gc->bar0 + GC570D_AUDIO_START);
		writel(control & ~GC570D_AUDIO_START_BIT,
		       gc->bar0 + GC570D_AUDIO_START);
		writel(GC570D_AUDIO_IRQ, gc->bar0 + GC570D_REG_IRQ_STATUS);
		wake_up_interruptible(&gc->audio_wait);
		return 0;
	default:
		return -EINVAL;
	}
}

static snd_pcm_uframes_t
gc570d_audio_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct gc570d_dev *gc = snd_pcm_substream_chip(substream);

	return bytes_to_frames(substream->runtime,
			       READ_ONCE(gc->audio_pcm_pos));
}

static const struct snd_pcm_ops gc570d_audio_pcm_ops = {
	.open = gc570d_audio_pcm_open,
	.close = gc570d_audio_pcm_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = gc570d_audio_pcm_hw_params,
	.hw_free = gc570d_audio_pcm_hw_free,
	.prepare = gc570d_audio_pcm_prepare,
	.trigger = gc570d_audio_pcm_trigger,
	.pointer = gc570d_audio_pcm_pointer,
};

/*
 * HDMI IN 1 follows the Windows receiver state machine after a link change:
 * VideoStable sends audio back through RequestAudio, WaitForReady and AudioOn.
 * capture_lock is held by the caller and the coherent staging buffers remain
 * allocated for the lifetime of the prepared PCM.
 */
int gc570d_audio0_recover_locked(struct gc570d_dev *gc)
{
	u32 control;
	int i;
	int ret;

	if (READ_ONCE(gc->suspended) || !READ_ONCE(gc->audio0_prepared) ||
	    !READ_ONCE(gc->audio0_running) ||
	    !READ_ONCE(gc->audio0_recovering))
		return 0;

	if (!READ_ONCE(gc->video0_streaming)) {
		ret = gc570d_it68051_video_output_on(gc);
		if (ret)
			return ret;
	}
	ret = gc570d_it68051_audio_output_set(gc, true);
	if (ret)
		return ret;

	control = readl(gc->bar0 + GC570D_AUDIO_START);
	writel(control & ~GC570D_AUDIO0_START_BIT,
	       gc->bar0 + GC570D_AUDIO_START);
	writel(GC570D_AUDIO0_IRQ | GC570D_AUDIO0_TERM_IRQ,
	       gc->bar0 + GC570D_REG_IRQ_STATUS);
	gc570d_reset_audio_channel(gc, GC570D_AUDIO0_CHANNEL);

	for (i = 0; i < 2; i++)
		memset(gc->audio0_pcm_buffer_cpu[i], GC570D_AUDIO_SENTINEL,
		       GC570D_AUDIO_BUFFER_SIZE);
	writel(0, gc->bar0 + GC570D_AUDIO0_FORMAT);
	writel(0, gc->bar0 + GC570D_AUDIO_GLOBAL_FORMAT);
	writel(GC570D_AUDIO_PERIOD_FRAMES, gc->bar0 + GC570D_AUDIO0_RATE);
	writel(lower_32_bits(gc->audio0_pcm_buffer_dma[0]),
	       gc->bar0 + GC570D_AUDIO0_BUFFER0);
	writel(upper_32_bits(gc->audio0_pcm_buffer_dma[0]),
	       gc->bar0 + GC570D_AUDIO0_BUFFER0 + 4);
	writel(lower_32_bits(gc->audio0_pcm_buffer_dma[1]),
	       gc->bar0 + GC570D_AUDIO0_BUFFER1);
	writel(upper_32_bits(gc->audio0_pcm_buffer_dma[1]),
	       gc->bar0 + GC570D_AUDIO0_BUFFER1 + 4);
	dma_wmb();

	atomic_set(&gc->audio0_irq_pending, 0);
	writel(GC570D_AUDIO0_IRQ | GC570D_AUDIO0_TERM_IRQ,
	       gc->bar0 + GC570D_REG_IRQ_STATUS);
	gc->audio0_term_baseline = atomic64_read(&gc->irq_audio0_term);
	gc->audio0_stop_pending = true;
	if (!READ_ONCE(gc->audio0_running))
		return -ESHUTDOWN;
	control = readl(gc->bar0 + GC570D_AUDIO_START);
	writel(control | GC570D_AUDIO0_START_BIT,
	       gc->bar0 + GC570D_AUDIO_START);
	gc->audio0_thread_error = 0;
	WRITE_ONCE(gc->audio0_recovering, false);
	wake_up_interruptible(&gc->audio0_wait);

	dev_info(&gc->pdev->dev,
		 "HDMI IN 1 audio signal stable; restarted the open PCM capture\n");
	return 1;
}

static int gc570d_audio0_pcm_push_period(struct gc570d_dev *gc,
					 const u8 *period)
{
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	size_t buffer_bytes;
	size_t first;
	size_t position;

	substream = READ_ONCE(gc->audio0_substream);
	if (!substream || !substream->runtime)
		return -ENODEV;
	runtime = substream->runtime;
	buffer_bytes = frames_to_bytes(runtime, runtime->buffer_size);
	position = READ_ONCE(gc->audio0_pcm_pos);
	first = min_t(size_t, GC570D_AUDIO_PERIOD_BYTES,
		      buffer_bytes - position);
	memcpy(runtime->dma_area + position, period, first);
	if (first < GC570D_AUDIO_PERIOD_BYTES)
		memcpy(runtime->dma_area, period + first,
		       GC570D_AUDIO_PERIOD_BYTES - first);
	position = (position + GC570D_AUDIO_PERIOD_BYTES) % buffer_bytes;
	WRITE_ONCE(gc->audio0_pcm_pos, position);
	snd_pcm_period_elapsed(substream);
	return 0;
}

static int gc570d_audio0_pcm_thread(void *data)
{
	struct gc570d_dev *gc = data;
	struct snd_pcm_substream *substream;
	u8 period[GC570D_AUDIO_PERIOD_BYTES];
	u32 irq_status;
	u32 index;
	unsigned long next_recovery = 0;
	int ret;

	while (!kthread_should_stop()) {
		wait_event_interruptible(gc->audio0_wait,
			kthread_should_stop() || READ_ONCE(gc->audio0_running));
		if (kthread_should_stop())
			break;
		if (READ_ONCE(gc->audio0_recovering)) {
			/* Keep ALSA moving before synchronous receiver recovery. */
			memset(period, 0, sizeof(period));
			ret = gc570d_audio0_pcm_push_period(gc, period);
			if (ret)
				goto xrun;

			if ((!READ_ONCE(gc->video0_streaming) ||
			     (!READ_ONCE(gc->video0_no_signal) &&
			      READ_ONCE(gc->video0_receiver_output_valid))) &&
			    time_after_eq(jiffies, next_recovery) &&
			    mutex_trylock(&gc->capture_lock)) {
				ret = gc570d_audio0_recover_locked(gc);
				mutex_unlock(&gc->capture_lock);
				if (ret > 0)
					continue;
				next_recovery = jiffies + msecs_to_jiffies(250);
			}
			usleep_range(9500, 10500);
			continue;
		}

		ret = readl_poll_timeout(gc->bar0 + GC570D_REG_IRQ_STATUS,
					 irq_status,
					 (irq_status & GC570D_AUDIO0_IRQ) ||
					 atomic_read(&gc->audio0_irq_pending) ||
					 READ_ONCE(gc->audio0_recovering),
					 100, 100000);
		if (!READ_ONCE(gc->audio0_running))
			continue;
		if (ret || READ_ONCE(gc->audio0_recovering)) {
			if (ret)
				gc->audio0_thread_error = ret;
			WRITE_ONCE(gc->audio0_recovering, true);
			atomic_set(&gc->audio0_irq_pending, 0);
			writel(readl(gc->bar0 + GC570D_AUDIO_START) &
			       ~GC570D_AUDIO0_START_BIT,
			       gc->bar0 + GC570D_AUDIO_START);
			writel(GC570D_AUDIO0_IRQ,
			       gc->bar0 + GC570D_REG_IRQ_STATUS);
			next_recovery = jiffies;
			dev_warn(&gc->pdev->dev,
				 "HDMI IN 1 audio DMA paused; keeping the open PCM alive with silence until the signal returns\n");
			continue;
		}
		atomic_set(&gc->audio0_irq_pending, 0);

		index = readl(gc->bar0 + 0x14) & 0x03;
		if (index < 1 || index > 2) {
			ret = -EIO;
			goto xrun;
		}
		ret = gc570d_audio_take_period(gc->audio0_pcm_buffer_cpu,
					       period);
		writel(GC570D_AUDIO0_IRQ, gc->bar0 + GC570D_REG_IRQ_STATUS);
		if (ret == -ENODATA)
			continue;
		if (ret)
			goto xrun;

		ret = gc570d_audio0_pcm_push_period(gc, period);
		if (ret)
			goto xrun;
		continue;

xrun:
		gc->audio0_thread_error = ret;
		WRITE_ONCE(gc->audio0_running, false);
		WRITE_ONCE(gc->audio0_recovering, false);
		atomic_set(&gc->audio0_irq_pending, 0);
		writel(readl(gc->bar0 + GC570D_AUDIO_START) &
		       ~GC570D_AUDIO0_START_BIT,
		       gc->bar0 + GC570D_AUDIO_START);
		writel(GC570D_AUDIO0_IRQ, gc->bar0 + GC570D_REG_IRQ_STATUS);
		substream = READ_ONCE(gc->audio0_substream);
		if (substream)
			snd_pcm_stop_xrun(substream);
	}

	return 0;
}

static int gc570d_audio0_pcm_open(struct snd_pcm_substream *substream)
{
	int ret;

	substream->runtime->hw = gc570d_audio_pcm_hardware;
	/* Receiver reconfiguration may take longer than ALSA's minimum wait. */
	substream->wait_time = GC570D_AUDIO_WAIT_TIME_MS;
	ret = snd_pcm_hw_constraint_integer(substream->runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
	return ret;
}

static int gc570d_audio0_pcm_close(struct snd_pcm_substream *substream)
{
	return 0;
}

static int gc570d_audio0_pcm_hw_params(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *params)
{
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
}

static void gc570d_audio0_finish_stop(struct gc570d_dev *gc)
{
	u32 status = readl(gc->bar0 + GC570D_REG_IRQ_STATUS);
	int ret = 0;

	if (gc->audio0_stop_pending) {
		ret = readl_poll_timeout(gc->bar0 + GC570D_REG_IRQ_STATUS,
					 status,
					 (status & GC570D_AUDIO0_TERM_IRQ) ||
					 atomic64_read(&gc->irq_audio0_term) !=
					 gc->audio0_term_baseline,
					 100, 100000);
		if (status & GC570D_AUDIO0_TERM_IRQ)
			atomic64_inc(&gc->irq_audio0_term);
		gc->audio0_stop_pending = false;
	}

	writel(GC570D_AUDIO0_IRQ | GC570D_AUDIO0_TERM_IRQ,
	       gc->bar0 + GC570D_REG_IRQ_STATUS);
	if (ret)
		dev_warn(&gc->pdev->dev,
			 "HDMI IN 1 audio DMA termination was not observed before release\n");
}

static int gc570d_audio0_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct gc570d_dev *gc = snd_pcm_substream_chip(substream);
	struct task_struct *thread;
	int i;

	WRITE_ONCE(gc->audio0_running, false);
	WRITE_ONCE(gc->audio0_recovering, false);
	atomic_set(&gc->audio0_irq_pending, 0);
	writel(readl(gc->bar0 + GC570D_AUDIO_START) &
	       ~GC570D_AUDIO0_START_BIT, gc->bar0 + GC570D_AUDIO_START);
	writel(GC570D_AUDIO0_IRQ, gc->bar0 + GC570D_REG_IRQ_STATUS);
	wake_up_interruptible(&gc->audio0_wait);
	thread = gc->audio0_thread;
	gc->audio0_thread = NULL;
	if (thread)
		kthread_stop(thread);

	mutex_lock(&gc->capture_lock);
	gc570d_audio0_finish_stop(gc);
	for (i = 0; i < 2; i++) {
		if (gc->audio0_pcm_buffer_cpu[i])
			dma_free_coherent(&gc->pdev->dev,
					  GC570D_AUDIO_BUFFER_SIZE,
					  gc->audio0_pcm_buffer_cpu[i],
					  gc->audio0_pcm_buffer_dma[i]);
		gc->audio0_pcm_buffer_cpu[i] = NULL;
	}
	if (gc->audio0_irq_active) {
		gc570d_video_irq_put_locked(gc);
		gc->audio0_irq_active = false;
	}
	if (gc->audio0_substream == substream)
		gc->audio0_substream = NULL;
	gc->audio0_prepared = false;
	if (!gc->video0_streaming && !gc->video_streaming &&
	    !gc->audio_prepared)
		pci_clear_master(gc->pdev);
	mutex_unlock(&gc->capture_lock);

	return snd_pcm_lib_free_pages(substream);
}

static bool gc570d_audio0_is_no_signal_error(int ret)
{
	return ret == -ENOLINK || ret == -ENODATA || ret == -EAGAIN ||
	       ret == -ERANGE || ret == -EINVAL || ret == -EILSEQ ||
	       ret == -EOPNOTSUPP;
}

static int gc570d_audio0_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct gc570d_dev *gc = snd_pcm_substream_chip(substream);
	bool recovering = false;
	int i;
	int ret = 0;

	mutex_lock(&gc->capture_lock);
	if (READ_ONCE(gc->suspended)) {
		ret = -EBUSY;
		goto out_unlock;
	}
	if (!gc->video0_streaming) {
		ret = gc570d_it68051_video_output_on(gc);
		if (ret && gc570d_audio0_is_no_signal_error(ret)) {
			recovering = true;
			ret = 0;
		} else if (ret) {
			goto out_unlock;
		}
	} else if (READ_ONCE(gc->video0_no_signal) ||
		   !READ_ONCE(gc->video0_receiver_output_valid)) {
		recovering = true;
	}
	if (!recovering) {
		ret = gc570d_it68051_audio_output_set(gc, true);
		if (ret && gc570d_audio0_is_no_signal_error(ret)) {
			recovering = true;
			ret = 0;
		} else if (ret) {
			goto out_unlock;
		}
	}
	if (!gc->audio0_prepared) {
		for (i = 0; i < 2; i++) {
			gc->audio0_pcm_buffer_cpu[i] =
				dma_alloc_coherent(&gc->pdev->dev,
						   GC570D_AUDIO_BUFFER_SIZE,
						   &gc->audio0_pcm_buffer_dma[i],
						   GFP_KERNEL);
			if (!gc->audio0_pcm_buffer_cpu[i]) {
				ret = -ENOMEM;
				goto out_free;
			}
		}
	}
	for (i = 0; i < 2; i++)
		memset(gc->audio0_pcm_buffer_cpu[i], GC570D_AUDIO_SENTINEL,
		       GC570D_AUDIO_BUFFER_SIZE);

	gc570d_reset_audio_channel(gc, GC570D_AUDIO0_CHANNEL);
	writel(GC570D_AUDIO0_IRQ | GC570D_AUDIO0_TERM_IRQ,
	       gc->bar0 + GC570D_REG_IRQ_STATUS);
	writel(0, gc->bar0 + GC570D_AUDIO0_FORMAT);
	writel(0, gc->bar0 + GC570D_AUDIO_GLOBAL_FORMAT);
	writel(GC570D_AUDIO_PERIOD_FRAMES, gc->bar0 + GC570D_AUDIO0_RATE);
	writel(lower_32_bits(gc->audio0_pcm_buffer_dma[0]),
	       gc->bar0 + GC570D_AUDIO0_BUFFER0);
	writel(upper_32_bits(gc->audio0_pcm_buffer_dma[0]),
	       gc->bar0 + GC570D_AUDIO0_BUFFER0 + 4);
	writel(lower_32_bits(gc->audio0_pcm_buffer_dma[1]),
	       gc->bar0 + GC570D_AUDIO0_BUFFER1);
	writel(upper_32_bits(gc->audio0_pcm_buffer_dma[1]),
	       gc->bar0 + GC570D_AUDIO0_BUFFER1 + 4);
	dma_wmb();

	gc->audio0_pcm_pos = 0;
	gc->audio0_thread_error = 0;
	WRITE_ONCE(gc->audio0_recovering, recovering);
	atomic_set(&gc->audio0_irq_pending, 0);
	gc->audio0_substream = substream;
	if (gc->audio0_prepared) {
		mutex_unlock(&gc->capture_lock);
		return 0;
	}
	gc->audio0_prepared = true;
	pci_set_master(gc->pdev);
	gc570d_video_irq_get_locked(gc);
	gc->audio0_irq_active = true;
	gc->audio0_thread = kthread_run(gc570d_audio0_pcm_thread, gc,
					"gc570d-audio0");
	if (IS_ERR(gc->audio0_thread)) {
		ret = PTR_ERR(gc->audio0_thread);
		gc->audio0_thread = NULL;
		gc->audio0_substream = NULL;
		gc->audio0_prepared = false;
		if (gc->audio0_irq_active) {
			gc570d_video_irq_put_locked(gc);
			gc->audio0_irq_active = false;
		}
		if (!gc->video0_streaming && !gc->video_streaming &&
		    !gc->audio_prepared)
			pci_clear_master(gc->pdev);
		goto out_free;
	}
	mutex_unlock(&gc->capture_lock);
	return 0;

out_free:
	for (i = 0; i < 2; i++) {
		if (gc->audio0_pcm_buffer_cpu[i])
			dma_free_coherent(&gc->pdev->dev,
					  GC570D_AUDIO_BUFFER_SIZE,
					  gc->audio0_pcm_buffer_cpu[i],
					  gc->audio0_pcm_buffer_dma[i]);
		gc->audio0_pcm_buffer_cpu[i] = NULL;
	}
out_unlock:
	mutex_unlock(&gc->capture_lock);
	return ret;
}

static int gc570d_audio0_pcm_trigger(struct snd_pcm_substream *substream,
				     int command)
{
	struct gc570d_dev *gc = snd_pcm_substream_chip(substream);
	u32 control;

	switch (command) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (!READ_ONCE(gc->audio0_prepared))
			return -EIO;
		atomic_set(&gc->audio0_irq_pending, 0);
		writel(GC570D_AUDIO0_IRQ | GC570D_AUDIO0_TERM_IRQ,
		       gc->bar0 + GC570D_REG_IRQ_STATUS);
		if (!READ_ONCE(gc->audio0_recovering)) {
			gc->audio0_term_baseline =
				atomic64_read(&gc->irq_audio0_term);
			gc->audio0_stop_pending = true;
			control = readl(gc->bar0 + GC570D_AUDIO_START);
			writel(control | GC570D_AUDIO0_START_BIT,
			       gc->bar0 + GC570D_AUDIO_START);
		}
		WRITE_ONCE(gc->audio0_running, true);
		wake_up_interruptible(&gc->audio0_wait);
		return 0;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		WRITE_ONCE(gc->audio0_running, false);
		WRITE_ONCE(gc->audio0_recovering, false);
		atomic_set(&gc->audio0_irq_pending, 0);
		control = readl(gc->bar0 + GC570D_AUDIO_START);
		writel(control & ~GC570D_AUDIO0_START_BIT,
		       gc->bar0 + GC570D_AUDIO_START);
		writel(GC570D_AUDIO0_IRQ, gc->bar0 + GC570D_REG_IRQ_STATUS);
		wake_up_interruptible(&gc->audio0_wait);
		return 0;
	default:
		return -EINVAL;
	}
}

static snd_pcm_uframes_t
gc570d_audio0_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct gc570d_dev *gc = snd_pcm_substream_chip(substream);

	return bytes_to_frames(substream->runtime,
			       READ_ONCE(gc->audio0_pcm_pos));
}

static const struct snd_pcm_ops gc570d_audio0_pcm_ops = {
	.open = gc570d_audio0_pcm_open,
	.close = gc570d_audio0_pcm_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = gc570d_audio0_pcm_hw_params,
	.hw_free = gc570d_audio0_pcm_hw_free,
	.prepare = gc570d_audio0_pcm_prepare,
	.trigger = gc570d_audio0_pcm_trigger,
	.pointer = gc570d_audio0_pcm_pointer,
};

int gc570d_audio_register(struct gc570d_dev *gc)
{
	struct snd_card *card;
	struct snd_pcm *pcm;
	struct snd_pcm *pcm0;
	int ret;

	ret = snd_devm_card_new(&gc->pdev->dev, -1, NULL, THIS_MODULE, 0,
				&card);
	if (ret)
		return ret;
	strscpy(card->driver, "gc570d", sizeof(card->driver));
	strscpy(card->shortname, "AVerMedia Live Gamer DUO",
		sizeof(card->shortname));
	snprintf(card->longname, sizeof(card->longname),
		 "AVerMedia Live Gamer DUO at %s", pci_name(gc->pdev));

	ret = snd_pcm_new(card, "HDMI 2 Capture Only", 0, 0, 1, &pcm);
	if (ret)
		return ret;
	pcm->private_data = gc;
	strscpy(pcm->name, "HDMI 2 (Capture Only)", sizeof(pcm->name));
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
			&gc570d_audio_pcm_ops);
	/* ALSA clamps buffer_bytes_max to each managed preallocation size. */
	ret = snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_VMALLOC,
					     NULL,
					     GC570D_AUDIO_PCM_BUFFER_MAX,
					     GC570D_AUDIO_PCM_BUFFER_MAX);
	if (ret)
		return ret;

	ret = snd_pcm_new(card, "HDMI 1 Passthrough", 1, 0, 1, &pcm0);
	if (ret)
		return ret;
	pcm0->private_data = gc;
	strscpy(pcm0->name, "HDMI 1 (Passthrough)", sizeof(pcm0->name));
	snd_pcm_set_ops(pcm0, SNDRV_PCM_STREAM_CAPTURE,
			&gc570d_audio0_pcm_ops);
	ret = snd_pcm_set_managed_buffer_all(pcm0, SNDRV_DMA_TYPE_VMALLOC,
					     NULL,
					     GC570D_AUDIO_PCM_BUFFER_MAX,
					     GC570D_AUDIO_PCM_BUFFER_MAX);
	if (ret)
		return ret;

	ret = snd_card_register(card);
	if (ret)
		return ret;
	gc->audio_card = card;
	gc->audio_pcm = pcm;
	gc->audio0_pcm = pcm0;
	dev_info(&gc->pdev->dev,
		 "registered HDMI IN 2 audio device 0 and HDMI IN 1 audio device 1 as card %d\n",
		 card->number);
	return 0;
}

static int gc570d_audio_pcm_status_show(struct seq_file *s, void *unused)
{
	struct gc570d_dev *gc = s->private;

	seq_printf(s,
		   "prepared=%u running=%u thread_error=%d position=%zu control=0x%08x\n",
		   READ_ONCE(gc->audio_prepared),
		   READ_ONCE(gc->audio_running), gc->audio_thread_error,
		   READ_ONCE(gc->audio_pcm_pos),
		   readl(gc->bar0 + GC570D_AUDIO_START));
	return 0;
}
GC570D_DEFINE_SHOW_ATTRIBUTE(gc570d_audio_pcm_status);

static int gc570d_audio0_pcm_status_show(struct seq_file *s, void *unused)
{
	struct gc570d_dev *gc = s->private;

	seq_printf(s,
		   "prepared=%u running=%u thread_error=%d position=%zu control=0x%08x recovering=%u\n",
		   READ_ONCE(gc->audio0_prepared),
		   READ_ONCE(gc->audio0_running), gc->audio0_thread_error,
		   READ_ONCE(gc->audio0_pcm_pos),
		   readl(gc->bar0 + GC570D_AUDIO_START),
		   READ_ONCE(gc->audio0_recovering));
	return 0;
}
GC570D_DEFINE_SHOW_ATTRIBUTE(gc570d_audio0_pcm_status);
