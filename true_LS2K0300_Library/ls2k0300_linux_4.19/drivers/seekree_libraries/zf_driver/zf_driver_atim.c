#include <linux/init.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/atmel_tc.h>
#include <linux/pwm.h>
#include <linux/of_device.h>
#include <linux/slab.h>

// 寄存器偏移
#define ATIM_CR1 		0x00
#define ATIM_CR2 		0x04
#define ATIM_SMCR 		0x08
#define ATIM_DIER 		0x0C
#define ATIM_SR 		0x10
#define ATIM_EGR 		0x14
#define ATIM_CCMR1 		0x18
#define ATIM_CCMR2 		0x1C
#define ATIM_CCER 		0x20
#define ATIM_CNT 		0x24
#define ATIM_PSC 		0x28
#define ATIM_ARR 		0x2C
#define ATIM_RCR 		0x30
#define ATIM_CCR1 		0x34
#define ATIM_CCR2 		0x38
#define ATIM_CCR3 		0x3C
#define ATIM_CCR4 		0x40
#define ATIM_BDTR 		0x44
#define ATIM_INSTA 		0x50


#define to_ls_pwm_atim_chip(_chip) container_of(_chip, struct ls_pwm_atim_chip, chip)
	
#define NS_IN_HZ (1000000000UL)
#define CPU_FRQ_PWM (50000000UL)

struct ls_pwm_atim_reg
{
	u32 arr;
	u32 ccr[4];
};

struct ls_pwm_atim_chip 
{
	struct pwm_chip chip;
	void __iomem *mmio_base;
	/* following registers used for suspend/resume */
	struct ls_pwm_atim_reg reg;
	
	u32 en_mark;
	u64	clock_frequency;
	// struct mutex lock;
	spinlock_t write_duty_lock;
};


static int ls_pwm_atim_set_polarity(struct pwm_chip *chip,
				    struct pwm_device *pwm,
				    enum pwm_polarity polarity)
{
	struct ls_pwm_atim_chip *ls_pwm = to_ls_pwm_atim_chip(chip);
	u16 val;
					
	// pwm->hwpwm = 0 -> ch1	
	// pwm->hwpwm = 1 -> ch1n	
	// pwm->hwpwm = 2 -> ch2	
	// pwm->hwpwm = 3 -> ch2n	
	// pwm->hwpwm = 4 -> ch3	
	// pwm->hwpwm = 5 -> ch3n	
	// pwm->hwpwm = 6 -> ch4	

	val = readl(ls_pwm->mmio_base + ATIM_CCER);
	switch (polarity) {
	case PWM_POLARITY_NORMAL:
		val &= ~(1 << (((pwm->hwpwm + 1) * 2 - 1)));
		break;
	case PWM_POLARITY_INVERSED:
		val |= (1 << (((pwm->hwpwm + 1) * 2 - 1)));
		break;
	default:
		break;
	}
	writel(val, ls_pwm->mmio_base + ATIM_CCER);
	return 0;
}

static void ls_pwm_atim_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct ls_pwm_atim_chip *ls_pwm = to_ls_pwm_atim_chip(chip);
	u32 cr1_reg = 0;
	u32 ccer_reg = 0;

	// 通道计数器
	writel(0, ls_pwm->mmio_base + (ATIM_CCR1 + (pwm->hwpwm) / 2 * 4));

	// 关闭通道
	ccer_reg = readl(ls_pwm->mmio_base + ATIM_CCER);
	ccer_reg &= ~(1 << ((pwm->hwpwm) * 2));
	writel(ccer_reg, ls_pwm->mmio_base + ATIM_CCER);

	// 如果所有通道都关闭，则停止该定时器工作
	ls_pwm->en_mark &= ~BIT(pwm->hwpwm);
	if (ls_pwm->en_mark == 0)
	{
		cr1_reg = readl(ls_pwm->mmio_base + ATIM_CR1);
		cr1_reg &= ~(1<<0);	// 关闭定时器计时
		writel(cr1_reg, ls_pwm->mmio_base + ATIM_CR1);
		writel(0x0, ls_pwm->mmio_base + ATIM_CNT);
	}
}

static int ls_pwm_atim_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct ls_pwm_atim_chip *ls_pwm = to_ls_pwm_atim_chip(chip);

	u32 cr1_reg, cr2_reg, ccmr_reg, ccer_reg;

	// todo:类似config函数中的特殊处理

	// 通道配置为输出
	if(pwm->hwpwm <= 3)
	{
		ccmr_reg = readl(ls_pwm->mmio_base + ATIM_CCMR1);
		ccmr_reg &= ~(0x3 << ((pwm->hwpwm) * 4));		// 配置为输出
		ccmr_reg |= 1 << ((pwm->hwpwm) / 2 * 8 + 3);	// 开启CCR寄存器预装载功能
		ccmr_reg |= 0x07 << ((pwm->hwpwm) / 2 * 8 + 4);	// 设置为PWM2模式
		writel(ccmr_reg, ls_pwm->mmio_base + ATIM_CCMR1);
	}
	else
	{
		ccmr_reg = readl(ls_pwm->mmio_base + ATIM_CCMR2);
		ccmr_reg &= ~(0x3 << ((pwm->hwpwm - 4) * 4));		// 配置为输出
		ccmr_reg |= 1 << ((pwm->hwpwm - 4) / 2 * 8 + 3);	// 开启CCR寄存器预装载功能
		ccmr_reg |= 0x07 << ((pwm->hwpwm - 4) / 2 * 8 + 4);	// 设置为PWM2模式
		writel(ccmr_reg, ls_pwm->mmio_base + ATIM_CCMR2);
	}


	// 使能通道
	ccer_reg = readl(ls_pwm->mmio_base + ATIM_CCER);
	ccer_reg |= 1 << ((pwm->hwpwm) * 2);
	writel(ccer_reg, ls_pwm->mmio_base + ATIM_CCER);

	if (ls_pwm->en_mark == 0) 
	{
		// 使能计数器
		cr1_reg = readl(ls_pwm->mmio_base + ATIM_CR1);
		cr1_reg |= 1<<0 | 1 << 7;	// CEN,使能计数 ,ARR 寄存器开启预装载
		writel(cr1_reg, ls_pwm->mmio_base + ATIM_CR1);

		cr2_reg = readl(ls_pwm->mmio_base + ATIM_CR2);
		cr2_reg |= 1<<0;	// 设置预装载
		writel(cr2_reg, ls_pwm->mmio_base + ATIM_CR2);
	}
	ls_pwm->en_mark |= BIT(pwm->hwpwm);
	return 0;
}

static int ls_pwm_atim_config(struct pwm_chip *chip, struct pwm_device *pwm,
			      int duty_ns, int period_ns)
{
	struct ls_pwm_atim_chip *ls_pwm = to_ls_pwm_atim_chip(chip);
	unsigned int arr_reg, ccr_reg;
	unsigned long long val0, val1;
	int temp_arr_reg = 0;
	unsigned long flags;

	// printk("period_ns = %d\r\n", period_ns);
	// printk("duty_ns = %d\r\n", duty_ns);
	// printk("pwm->hwpwm = %d\r\n", pwm->hwpwm);
	
	// printk("ATIM_ARR = 0x%x \r\n", readl(ls_pwm->mmio_base + ATIM_ARR));
	// printk("ATIM_CR1 = 0x%x \r\n", readl(ls_pwm->mmio_base + ATIM_CR1));
	// printk("ATIM_CR2 = 0x%x \r\n", readl(ls_pwm->mmio_base + ATIM_CR2));
	// printk("ATIM_CCMR1 = 0x%x \r\n", readl(ls_pwm->mmio_base + ATIM_CCMR1));
	// printk("ATIM_CCMR2 = 0x%x \r\n", readl(ls_pwm->mmio_base + ATIM_CCMR2));
	// printk("ATIM_CCER = 0x%x \r\n", readl(ls_pwm->mmio_base + ATIM_CCER));

	// printk("ATIM_CNT = 0x%x \r\n", readl(ls_pwm->mmio_base + ATIM_CNT));

	// printk("ATIM_CCR1 = 0x%x \r\n", readl(ls_pwm->mmio_base + ATIM_CCR1));
	// printk("ATIM_CCR2 = 0x%x \r\n", readl(ls_pwm->mmio_base + ATIM_CCR2));
	// printk("ATIM_CCR3 = 0x%x \r\n", readl(ls_pwm->mmio_base + ATIM_CCR3));
	// printk("ATIM_CCR4 = 0x%x \r\n", readl(ls_pwm->mmio_base + ATIM_CCR4));

	if (period_ns > NS_IN_HZ || duty_ns > NS_IN_HZ)
		return -ERANGE;

	if(duty_ns > period_ns)
		// 你想想怎么可能？
		return -ERANGE;

	val0 = ls_pwm->clock_frequency * period_ns;
	do_div(val0, NSEC_PER_SEC);
	arr_reg = val0 < 2 ? 2 : val0;

	val1 = ls_pwm->clock_frequency * duty_ns;
	do_div(val1, NSEC_PER_SEC);
	ccr_reg = val1;// < 1 ? 1 : val1;

	// 上锁，防止多通道读写异常
	// mutex_lock(&ls_pwm->lock);
	spin_lock_irqsave(&ls_pwm->write_duty_lock, flags);

	// 需要更新arr寄存器
	temp_arr_reg = readl(ls_pwm->mmio_base + ATIM_ARR);
	if(temp_arr_reg != arr_reg)
	{
		writel(arr_reg, ls_pwm->mmio_base + ATIM_ARR);
		ls_pwm->reg.arr = arr_reg;
	}

	// 这是通道计数器
	writel(ccr_reg, ls_pwm->mmio_base + (ATIM_CCR1 + (pwm->hwpwm) / 2 * 4));
	// mutex_unlock(&ls_pwm->lock);
	spin_unlock_irqrestore(&ls_pwm->write_duty_lock, flags);

	return 0;
}

static unsigned int ls_pwm_atim_reg_to_ns(struct ls_pwm_atim_chip *ls_pwm,
					  unsigned int reg)
{
	unsigned long long val;

	val = reg * NSEC_PER_SEC;
	do_div(val, ls_pwm->clock_frequency);
	return val;
}

void ls_pwm_atim_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
			   struct pwm_state *state)
{
	struct ls_pwm_atim_chip *ls_pwm = to_ls_pwm_atim_chip(chip);
	unsigned int arr_reg, ccr_reg;
	u32 ccer_reg;

	/*
	 * period_ns = arr_reg *NSEC_PER_SEC /ls_pwm->clock_frequency.
	 */
	arr_reg = readl(ls_pwm->mmio_base + ATIM_ARR);
	state->period = ls_pwm_atim_reg_to_ns(ls_pwm, arr_reg);
	
	ccr_reg = readl(ls_pwm->mmio_base + (ATIM_CCR1 + (pwm->hwpwm) / 2 * 4));
	state->duty_cycle = ls_pwm_atim_reg_to_ns(ls_pwm, ccr_reg);

	ccer_reg = readl(ls_pwm->mmio_base + ATIM_CCER);

	state->polarity = (ccer_reg & (1 << ((pwm->hwpwm) * 2 + 1))) ? PWM_POLARITY_INVERSED : PWM_POLARITY_NORMAL;

	state->enabled = (ccer_reg & (1 << ((pwm->hwpwm) * 2))) ? true : false;

	ls_pwm->reg.arr = arr_reg;
}

static const struct pwm_ops ls_pwm_atim_ops = {
	.config = ls_pwm_atim_config,
	.set_polarity = ls_pwm_atim_set_polarity,
	.enable = ls_pwm_atim_enable,
	.disable = ls_pwm_atim_disable,
	.get_state = ls_pwm_atim_get_state,
	.owner = THIS_MODULE,
};

static int ls_pwm_atim_probe(struct platform_device *pdev)
{
	struct ls_pwm_atim_chip *pwm;
	struct resource *mem;
	int err;
	struct device_node *np = pdev->dev.of_node;
	u32 clk;

	pwm = devm_kzalloc(&pdev->dev, sizeof(*pwm), GFP_KERNEL);
	if (!pwm) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	pwm->en_mark = 0;

	pwm->chip.dev = &pdev->dev;
	pwm->chip.ops = &ls_pwm_atim_ops;
	pwm->chip.base = -1;
	pwm->chip.npwm = 7;

	if (!(of_property_read_u32(np, "clock-frequency", &clk)))
		pwm->clock_frequency = clk;
	else
		pwm->clock_frequency = CPU_FRQ_PWM;

	dev_info(&pdev->dev, "pwm->clock_frequency=%llu", pwm->clock_frequency);

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(&pdev->dev, "no mem resource?\n");
		return -ENODEV;
	}
	pwm->mmio_base = devm_ioremap_resource(&pdev->dev, mem);
	if (!pwm->mmio_base) {
		dev_err(&pdev->dev, "mmio_base is null\n");
		return -ENOMEM;
	}

	err = pwmchip_add(&pwm->chip);
	if (err < 0) {
		dev_err(&pdev->dev, "pwmchip_add() failed: %d\n", err);
		return err;
	}

	platform_set_drvdata(pdev, pwm);

	writel(1<<15, pwm->mmio_base + ATIM_BDTR);
	
	writel(0, pwm->mmio_base + ATIM_CCER);
	writel(0, pwm->mmio_base + ATIM_CR1);
	writel(0, pwm->mmio_base + ATIM_CR2);
	writel(0, pwm->mmio_base + ATIM_CCMR1);
	writel(0, pwm->mmio_base + ATIM_CCMR2);
	
	// 初始化互斥锁，用于保护对设备的并发访问
	// mutex_init(&pwm->lock);
	spin_lock_init(&pwm->write_duty_lock);
	
	dev_dbg(&pdev->dev, "pwm probe successful\n");
	return 0;
}

static int ls_pwm_atim_remove(struct platform_device *pdev)
{
	struct ls_pwm_atim_chip *pwm = platform_get_drvdata(pdev);
	int err;
	if (!pwm)
		return -ENODEV;
	err = pwmchip_remove(&pwm->chip);
	if (err < 0)
		return err;

	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id ls_pwm_atim_id_table[] = {
	{ .compatible = "loongson,ls300-pwm-atim" },
	{},
};
MODULE_DEVICE_TABLE(of, ls_pwm_atim_id_table);
#endif

static struct platform_driver ls_pwm_atim_driver = {
	.driver = {
		.name = "ls-pwm-atim",
		.owner = THIS_MODULE,
		.bus = &platform_bus_type,

#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(ls_pwm_atim_id_table),
#endif
	},
	.probe = ls_pwm_atim_probe,
	.remove = ls_pwm_atim_remove,
};
module_platform_driver(ls_pwm_atim_driver);


MODULE_DESCRIPTION("Loongson Atimer Pwm Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("seekfree_bigW");